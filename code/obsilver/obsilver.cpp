//NOTE(adm244): the code is in a terrible state (thanks to obse and oop crap)
// don't want to have anything to do with it more than it's necessary to make this thing work

#include "obse/PluginAPI.h"
#include "obse/GameAPI.h"
//#include "obse/Script.h"
//#include "obse/GameObjects.h"
//#include "obse/Utilities.h"

#include <string>
#include <windows.h>

#define SCRIPTNAME "obsilver"
#define CONFIGFILE "obsilver.ini"
#define LOGFILE "obsilver.log"

#define MAX_SECTION 32767
#define MAX_FILENAME 128
#define MAX_BATCHES 20
#define MAX_STRING 255

struct BatchData{
  char filename[MAX_FILENAME];
  int key;
  BOOL enabled;
};

static HMODULE g_hModule = NULL;
static HANDLE g_MainLoopThread = NULL;

static BatchData batches[MAX_BATCHES];
static int batchnum;

static BOOL not_initialized = TRUE;
static BOOL main_loop_running = FALSE;
static BOOL main_loop_should_close = FALSE;
static BOOL keys_active;
static BYTE key_disable;

IDebugLog gLog(LOGFILE);

PluginHandle g_pluginHandle = kPluginHandle_Invalid;
OBSEConsoleInterface *g_ConsoleInterface = NULL;
OBSEScriptInterface *g_ScriptInterface = NULL;
OBSEArrayVarInterface *g_ArrayVarInterface = NULL;

bool GetKeyPressed(BYTE key)
{
  SHORT keystate = (SHORT)GetAsyncKeyState(key);
  return( (keystate & 0x8000) > 0 );
}

std::string GetPathFromFilename(std::string filename)
{
  return filename.substr(0, filename.rfind("\\") + 1);
}

DWORD IniReadInt(char *inifile, char *section, char *param, DWORD def)
{
  char curdir[MAX_PATH];
  GetModuleFileNameA(g_hModule, curdir, sizeof(curdir));
  std::string fname = GetPathFromFilename(curdir) + inifile;
  return GetPrivateProfileIntA(section, param, def, fname.c_str());
}

//NOTE(adm244): retrieves all key-value pairs from specified section of ini file and stores it in buffer
DWORD IniReadSection(char *inifile, char *section, char *buffer, DWORD bufsize)
{
  char curdir[MAX_PATH];
  GetModuleFileNameA(g_hModule, curdir, sizeof(curdir));
  std::string fname = GetPathFromFilename(curdir) + inifile;
  return GetPrivateProfileSectionA(section, buffer, bufsize, fname.c_str());
}

//NOTE(adm244): loads a list of batch files and keys that activate them
BOOL InitBatchFiles(BatchData *batches, int *num)
{
  char buf[MAX_SECTION];
  char *str = buf;
  int index = 0;
  
  IniReadSection(CONFIGFILE, "batch", buf, MAX_SECTION);
  
  while( TRUE ){
    char *p = strrchr(str, '=');
    
    if( p && (index < MAX_BATCHES) ){
      char *endptr;
      *p++ = '\0';
      
      strcpy(batches[index].filename, str);
      batches[index].key = (int)strtol(p, &endptr, 0);
      batches[index].enabled = TRUE;
      
      _MESSAGE("%s activates with 0x%02X", batches[index].filename, batches[index].key);
      
      str = strchr(p, '\0');
      str++;
      
      index++;
    } else{
      break;
    }
  }
  
  *num = index;
  return(index > 0);
}

bool main_init()
{
  BOOL bres = InitBatchFiles(batches, &batchnum);
  
  keys_active = TRUE;
  key_disable = IniReadInt(CONFIGFILE, "keys", "iKeyToggle", 0x24);
  
  _MESSAGE("[INFO] %s script launched", SCRIPTNAME);
  
  if( bres ){
    _MESSAGE("[INFO] %d batch files initialized", batchnum);
  } else{
    _ERROR("[ERROR] Batch files failed to initialize");
    return(FALSE);
  }
  
  return(TRUE);
}

DWORD WINAPI main_loop(LPVOID lpParam)
{
  while( !main_loop_should_close ){
    if( main_loop_running ){
      if( GetKeyPressed(key_disable) ){
        if( keys_active ){
          QueueUIMessage_2("[INFO] Commands disabled", 5, NULL, NULL);
          //_MESSAGE("[INFO] Commands disabled");
        } else{
          QueueUIMessage_2("[INFO] Commands enabled", 5, NULL, NULL);
          //_MESSAGE("[INFO] Commands enabled");
        }
        keys_active = !keys_active;
      }
      
      if( keys_active ){
        for( int i = 0; i < batchnum; ++i ){
          if( GetKeyPressed(batches[i].key) ){
            if( !batches[i].enabled ){
              continue;
            }
            batches[i].enabled = FALSE;
            
            char str[MAX_STRING];
            sprintf(str, "RunBatchScript \"%s.txt\"\0", batches[i].filename);
            //sprintf(str, "bat \"%s.txt\"", batches[i].filename);

            //0x0098629E - console bat?
            //push mask and actual string on stack then call it?
            //return to?
            //ThisStdCall(UInt32 _f,void* _t)
            
            //IMPORTANT(adm244): have no idea why, but this thing crashes Oblivion
            // every time it tries to execute a command that spawns something (usually on the second try).
            // obse people, I HAVE questions...
            g_ConsoleInterface->RunScriptLine(str);
            //QueueUIMessage(str, 0, 0, 5);
            
            /*
            // create a Script object
            UInt8	scriptObjBuf[sizeof(Script)];
            Script	* tempScriptObj = (Script *)scriptObjBuf;

            void	* scriptState = GetGlobalScriptStateObj();

            tempScriptObj->Constructor();
            tempScriptObj->MarkAsTemporary();
            tempScriptObj->SetText(buf);
            bool bResult = tempScriptObj->CompileAndRun(*((void**)scriptState), 1, callingObj);
            tempScriptObj->StaticDestructor();
            */
            
            //UInt8 scriptObjBuf[sizeof(Script)];
            //Script *scriptObj = (Script *)scriptObjBuf;
            
            //void *scriptState = GetGlobalScriptStateObj();
            
            //NOTE(adm244): copied from Commands_Console.cpp of OBSE v21
            // ### need to add a guard as the state object can be NULL sometimes (no idea why)
            //if(scriptState && *((void**)scriptState)){
              //scriptObj->Constructor();
              //scriptObj->MarkAsTemporary();
              //scriptObj->SetText(str);
              //scriptObj->SetText("RunBatchScript \"obgold.txt\"");
              //scriptObj->CompileAndRun(*((void**)scriptState), 1, NULL);
              //scriptObj->CompileAndRun(NULL, 0, NULL);
              //scriptObj->StaticDestructor();
            //}
            
            //(* CallFunction)(Script* funcScript, TESObjectREFR* callingObj, TESObjectREFR* container, OBSEArrayVarInterface::Element * result, UInt8 numArgs, ...);
            
            //g_ArrayVarInterface::Element elem();
            //OBSEArrayVarInterface::Element elem;
            //PlayerCharacter *pc = (*g_thePlayer);

            //TESClass *playerClass = pc->GetPlayerClass();
            //TESObjectREFR *playerRef = OBLIVION_CAST((void *)playerClass, TESClass, TESObjectREFR);

            //g_ScriptInterface->CallFunction(scriptObj, NULL, NULL, &elem, 1, batches[i].filename);
            
            //scriptObj->StaticDestructor();
            //char msg[MAX_STRING] = "!";
            //strcat(msg, batches[i].filename);
            //strcat(msg, " was successeful");
            
            char msg[MAX_STRING];
            sprintf(msg, "!%s was successeful", batches[i].filename);
            
            QueueUIMessage_2(msg, 5, NULL, NULL);
            _MESSAGE(msg);
          } else{
            batches[i].enabled = TRUE;
          }
        }
      }
    }
    
    //Sleep(250);
  }
  
  _MESSAGE("Thread exited");
  return(0);
}

void MessageHandler(OBSEMessagingInterface::Message* msg)
{
  switch(msg->type){
    case OBSEMessagingInterface::kMessage_ExitGame:
    case OBSEMessagingInterface::kMessage_ExitGame_Console:{
      main_loop_should_close = TRUE;
    } break;
    
    case OBSEMessagingInterface::kMessage_LoadGame:
    case OBSEMessagingInterface::kMessage_ExitToMainMenu:{
      _MESSAGE("MainLoop deactivation");
      main_loop_running = FALSE;
    } break;

    case OBSEMessagingInterface::kMessage_PostLoadGame:{
      _MESSAGE("MainLoop activation");
      main_loop_running = TRUE;
      
      if( not_initialized ){
        char msg[MAX_STRING];
        
        sprintf(msg, "[INFO] %s script launched", SCRIPTNAME);
        QueueUIMessage_2(msg, 3, NULL, NULL);
        
        sprintf(msg, "[INFO] %d batch files initialized", batchnum);
        QueueUIMessage_2(msg, 5, NULL, NULL);
        
        not_initialized = FALSE;
      }
    } break;
  }
}

extern "C" bool OBSEPlugin_Query(const OBSEInterface * obse, PluginInfo * info)
{
  _MESSAGE("OBSEPlugin_Query");

  info->infoVersion = PluginInfo::kInfoVersion;
  info->name = SCRIPTNAME;
  info->version = 1;

  if(!obse->isEditor) {
    if(obse->obseVersion < OBSE_VERSION_INTEGER) {
      _ERROR("OBSE version too old (got %08X expected at least %08X)", obse->obseVersion, OBSE_VERSION_INTEGER);
      return(FALSE);
    }

#if OBLIVION
    if(obse->oblivionVersion != OBLIVION_VERSION) {
      _ERROR("incorrect Oblivion version (got %08X need %08X)", obse->oblivionVersion, OBLIVION_VERSION);
      return(FALSE);
    }
#endif
    
    g_ConsoleInterface = (OBSEConsoleInterface *)obse->QueryInterface(kInterface_Console);
    if(!g_ConsoleInterface){
      _ERROR("console interface not found");
      return(FALSE);
    }
    
    if(g_ConsoleInterface->version < OBSEConsoleInterface::kVersion){
      _ERROR("incorrect console interface version found (got %08X need %08X)", g_ConsoleInterface->version, OBSEConsoleInterface::kVersion);
      _ERROR("console interface invalid version");
      return(FALSE);
    }
    
    g_ScriptInterface = (OBSEScriptInterface *)obse->QueryInterface(kInterface_Script);
    if(!g_ScriptInterface){
      _ERROR("script interface not found");
      return(FALSE);
    }
    
    g_ArrayVarInterface = (OBSEArrayVarInterface *)obse->QueryInterface(kInterface_ArrayVar);
    if(!g_ArrayVarInterface){
      _ERROR("arrayvar interface not found");
      return(FALSE);
    }
  }

  _MESSAGE("Success");
  return(TRUE);
}

extern "C" bool OBSEPlugin_Load(const OBSEInterface * obse)
{
  _MESSAGE("OBSEPlugin_Load");

  g_pluginHandle = obse->GetPluginHandle();

  OBSEMessagingInterface *msgIntfc = (OBSEMessagingInterface *)obse->QueryInterface(kInterface_Messaging);
  msgIntfc->RegisterListener(g_pluginHandle, "OBSE", MessageHandler);
  
  if( !main_init() ){
    return(FALSE);
  }
  
  g_MainLoopThread = CreateThread(NULL, 0, main_loop, NULL, 0, NULL);
  _MESSAGE("Thread created");
  
  _MESSAGE("Success");
  return(TRUE);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved)
{
  switch (fdwReason)
  {
    case DLL_PROCESS_ATTACH:
    {
      g_hModule = hModule;
      break;
    }
    
    case DLL_PROCESS_DETACH:
    {
      //NOTE(adm244): might be a bad idea, haven't really worked with threads yet
      if( g_MainLoopThread ){
        CloseHandle(g_MainLoopThread);
      }
      break;
    }
  }
  return(TRUE);
}
