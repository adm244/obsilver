/*
THIS LICENCE TEXT IS ONLY APPLICABLE FOR THIS SPECIFIC SOURCE FILE ONLY.
BE WARE THAT OBSE ITSELF MIGHT USE A DIFFERENT KIND OF LICENSING.

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

//FIX(adm244): replace std::string with c_string
//FIX(adm244): (DONE) move message of command activation above the RunScriptLine(..)

//TODO(adm244): (PARTIALLY DONE) disable QueueUIMessage_2 for the time RunScriptLine(..) is executing
// will prevent UI messages from bloating the message queue and will fix sound distortions
// look for QueueUIMessage_2 address and maybe patch this funtion so we could disable it
//NOTE(adm244): suppressed messages but not sound distortions

//TODO(adm244): hack around the rutony chat bug that allows to activate all commands at once
// a simple check if GetKeyPressed for all batches is active should do the trick
// OR block the execution if half of the batch files are activated on the same frame

//TODO(adm244): (SOUND ONLY DONE) attach sound and texture(maybe) to the message of command activation
//TODO(adm244): groups of commands and cooldown on the groups instead of individual commands
// preserve the ability to set cooldowns on each commands (no grouping)

//TODO(adm244): (TIMER ONLY DONE) implement cooldown system (by timer or activations count)
//TODO(adm244): implement a set chance for each command to activate?

#include <string>
#include <windows.h>

#include "obse/PluginAPI.h"
#include "obse/GameAPI.h"
#include "obse_common/SafeWrite.h"

#define byte BYTE
#define internal static

#define SCRIPTNAME "obsilver"
#define CONFIGFILE "obsilver.ini"
#define LOGFILE "obsilver.log"

#define MAX_SECTION 32767
#define MAX_FILENAME 128
#define MAX_BATCHES 30
#define MAX_GROUPES 30
#define MAX_STRING 255

struct BatchGroup{
  char name[MAX_STRING];
  int timeout;
  bool enabled;
};

struct BatchData{
  BatchGroup *group;
  
  char filename[MAX_FILENAME];
  int keycode;
  int timeout;
  bool allowed;
  bool enabled;
};

internal HMODULE g_hModule = NULL;
internal HANDLE g_Timer = NULL;
internal HANDLE g_TimerQueue = NULL;
internal PluginHandle g_pluginHandle = kPluginHandle_Invalid;

internal IDebugLog gLog(LOGFILE);
internal OBSEConsoleInterface *g_ConsoleInterface = NULL;
internal OBSEScriptInterface *g_ScriptInterface = NULL;
internal OBSEArrayVarInterface *g_ArrayVarInterface = NULL;

//NOTE(adm244): addresses for hooks (oblivion 1.2.416)
internal const UInt32 mainloop_hook_patch_address = 0x0040F1A3;
internal const UInt32 mainloop_hook_return_address = 0x0040F1A8;
internal const UInt32 showuimessage_patch_address = 0x0057ACC0;
internal const UInt32 showuimessage_2_patch_address = 0x0057ADD0;

internal BatchGroup groups[MAX_GROUPES];
internal BatchData batches[MAX_BATCHES];
internal int batchnum;

internal bool not_initialized;
internal bool main_loop_running;
internal bool keys_active;
internal byte key_disable;

//NOTE(adm244): returns whenever key is pressed or not
bool GetKeyPressed(byte key)
{
  short keystate = (short)GetAsyncKeyState(key);
  return( (keystate & 0x8000) > 0 );
}

//NOTE(adm244): retrieves folder path from full path
std::string GetPathFromFilename(std::string filename)
{
  return filename.substr(0, filename.rfind("\\") + 1);
}

//NOTE(adm244): retrieves an integer value from specified section and key of ini file
int IniReadInt(char *inifile, char *section, char *param, int def)
{
  char curdir[MAX_PATH];
  GetModuleFileNameA(g_hModule, curdir, sizeof(curdir));
  std::string fname = GetPathFromFilename(curdir) + inifile;
  return GetPrivateProfileIntA(section, param, def, fname.c_str());
}

//NOTE(adm244): retrieves all key-value pairs from specified section of ini file and stores it in buffer
int IniReadSection(char *inifile, char *section, char *buffer, int bufsize)
{
  char curdir[MAX_PATH];
  GetModuleFileNameA(g_hModule, curdir, sizeof(curdir));
  std::string fname = GetPathFromFilename(curdir) + inifile;
  return GetPrivateProfileSectionA(section, buffer, bufsize, fname.c_str());
}

void ParseGroupData(char *groupname, BatchData *batch)
{
  char buf[MAX_SECTION];
  char *str = buf;
  int index;
  
  IniReadSection(CONFIGFILE, "groups", buf, MAX_SECTION);
  
  //_MESSAGE("Parsing group...");
  while( true ){
    /*
      summon=0,60\0help=1,30\0
    */
    
    char *p = strrchr(str, '=');
    if( p ){
      char *endptr;
      *p++ = '\0';
      
      if( !strcmp(groupname, str) ){
        index = (int)strtol(p, &endptr, 10);
        
        p = strchr(p, ',');
        *p++ = '\0';
        
        strcpy(groups[index].name, groupname);
        groups[index].timeout = (int)strtol(p, &endptr, 10);
        groups[index].enabled = true;
        
        batch->group = &groups[index];
        
        _MESSAGE("\"%s\" is in group \"%s\" with timeout %d seconds", batch->filename, batch->group->name, groups[index].timeout);
        
        break;
      }
      
      str = strchr(p, '\0');
      str++;
    } else{
      break;
    }
  }
  //_MESSAGE("OK");
}

//NOTE(adm244): loads a list of batch files and keys that activate them
bool InitBatchFiles(BatchData *batches, int *num)
{
  char buf[MAX_SECTION];
  char *str = buf;
  int index = 0;

  /*char *curstr = str;
  char *p = NULL;
  char *endptr;*/

  IniReadSection(CONFIGFILE, "batch", buf, MAX_SECTION);

  //FIX(adm244): very naive parser implementation, do error checks
  _MESSAGE("Loading batch files...");
  while( true ){
    /*
      oblvlup=0x23,180\0obgold=0x2E,60\0
    */
    char *p = strrchr(str, '=');

    if( p && (index < MAX_BATCHES) ){
      char *endptr;
      *p++ = '\0';

      strcpy(batches[index].filename, str);
      batches[index].keycode = (int)strtol(p, &endptr, 0);

      p = strchr(p, ',');
      *p++ = '\0';

      batches[index].timeout = (int)strtol(p, &endptr, 10);
      
      if( batches[index].timeout == 0 ){
        //char groupname[MAX_STRING];
        
        p = strchr(p, ',');
        *p++ = '\0';
        
        //strcpy(groupname, p);
        ParseGroupData(p, &batches[index]);
      } else{
        batches[index].group = NULL;
        _MESSAGE("%s activates with 0x%02X, timeout for %d seconds", batches[index].filename, batches[index].keycode, batches[index].timeout);
      }

      batches[index].allowed = true;
      batches[index].enabled = true;

      str = strchr(p, '\0');
      str++;

      index++;
    } else{
      break;
    }
  }
  _MESSAGE("OK");

  *num = index;
  return(index > 0);
}

VOID CALLBACK timer_callback(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
  BatchData *data = (BatchData *)lpParam;
  char msg[MAX_STRING];
  
  if( data->group ){
    data->group->enabled = true;
    sprintf(msg, "%s group enabled", data->group->name);
  } else{
    data->enabled = true;
    sprintf(msg, "!%s enabled", data->filename);
  }

  QueueUIMessage_2(msg, 5, NULL, NULL);
}

//NOTE(adm244): enables\disables any ui messages that shows up on top-left of the screen
//NOTE(adm244): it doesn't disable some sounds (skill increase messages would still have sound played)
//NOTE(adm244): need to be very careful since obse can patch this address as well
// suppress = true - disables ui messages
// suppress = false - enables ui messages
void SuppressUIMessages(bool suppress)
{
  //NOTE(adm244): oblivion 1.2.416
  // QUIMsg_2PatchAddr = 0x0057ADD0
  // Original instruction: 0xD9EE (fldz - (fpu x87) load +0.0)
  // Patch: 0xC390 (ret, nop)
  //
  // QUIMsg_PatchAddr = 0x0057ACC0
  // Original instruction: 0x51 (push eax)
  // Patch: 0xC3 (ret)

  //IMPORTANT(adm244): looks like SafeWrite16(..) is all messed up
  // it writes the highest byte twice instead of two bytes as it should
  if( suppress ){
    SafeWrite8(showuimessage_patch_address, 0xC3);
    SafeWrite8(showuimessage_2_patch_address, 0xC3);
    SafeWrite8(showuimessage_2_patch_address + 1, 0x90);
  } else{
    SafeWrite8(showuimessage_patch_address, 0x51);
    SafeWrite8(showuimessage_2_patch_address, 0xD9);
    SafeWrite8(showuimessage_2_patch_address + 1, 0xEE);
  }
}

//NOTE(adm244): initializes plugin data
bool main_init()
{
  bool bresult = InitBatchFiles(batches, &batchnum);

  if( bresult ){
    _MESSAGE("%d batch files initialized", batchnum);
  } else{
    _ERROR("Batch files failed to initialize");
    return(false);
  }

  not_initialized = true;
  main_loop_running = false;

  keys_active = true;
  key_disable = IniReadInt(CONFIGFILE, "keys", "iKeyToggle", 0x24);

  return(true);
}

//NOTE(adm244): "oblivion" calls this function every frame if window is active
static void mainloop()
{
  if( main_loop_running ){
    //FIX(adm244): add a key press guard here
    if( GetKeyPressed(key_disable) ){
      if( keys_active ){
        QueueUIMessage_2("[INFO] Commands disabled", 5, NULL, NULL);
        _MESSAGE("[INFO] Commands disabled");
      } else{
        QueueUIMessage_2("[INFO] Commands enabled", 5, NULL, NULL);
        _MESSAGE("[INFO] Commands enabled");
      }
      keys_active = !keys_active;
    }

    if( keys_active ){
      for( int i = 0; i < batchnum; ++i ){
        if( batches[i].enabled && GetKeyPressed(batches[i].keycode) ){
          if( !batches[i].allowed ){
            continue;
          }
          batches[i].allowed = false;
          
          
          char msg[MAX_STRING];
          if( batches[i].group ){
            if( !batches[i].group->enabled ){
              continue;
            }
            
            batches[i].group->enabled = false;
            CreateTimerQueueTimer(&g_Timer, g_TimerQueue, (WAITORTIMERCALLBACK)timer_callback,
                                  &batches[i], batches[i].group->timeout * 1000, 0, 0);
            
            sprintf(msg, "!%s activated. Timeout on group %s for %d seconds.", batches[i].filename, batches[i].group->name, batches[i].group->timeout);
          } else{
            batches[i].enabled = false;
            CreateTimerQueueTimer(&g_Timer, g_TimerQueue, (WAITORTIMERCALLBACK)timer_callback,
                                  &batches[i], batches[i].timeout * 1000, 0, 0);

            sprintf(msg, "!%s activated. Timeout for %d seconds.", batches[i].filename, batches[i].timeout);
          }
          
          //TODO(adm244): load sound string from ini file
          QueueUIMessage_2(msg, 5, NULL, "UIQuestUpdate");
          _MESSAGE(msg);

          char str[MAX_STRING];
          sprintf(str, "RunBatchScript \"%s.txt\"\0", batches[i].filename);

          SuppressUIMessages(true);
          g_ConsoleInterface->RunScriptLine(str);
          SuppressUIMessages(false);
        } else{
          batches[i].allowed = true;
        }
      }
    }
  }
}

//NOTE(adm244): these instructions will be executed in the oblivion mainloop
// right after obse executes it's own "mainloop"
static __declspec(naked) void mainloop_hook()
{
  __asm{
    pushad
    call mainloop
    popad

    //NOTE(adm244): original instructions
    mov ecx, [eax]
    mov edx, [ecx + 0x0C]

    jmp [mainloop_hook_return_address]
  }
}

//NOTE(adm244): handles messages from obse
void MessageHandler(OBSEMessagingInterface::Message* msg)
{
  switch(msg->type){
    case OBSEMessagingInterface::kMessage_LoadGame:
    case OBSEMessagingInterface::kMessage_ExitToMainMenu:{
      _MESSAGE("MainLoop deactivation");
      main_loop_running = false;
    } break;

    case OBSEMessagingInterface::kMessage_PostLoadGame:{
      _MESSAGE("MainLoop activation");
      main_loop_running = true;

      if( not_initialized ){
        char msg[MAX_STRING];

        sprintf(msg, "[INFO] %s script launched", SCRIPTNAME);
        QueueUIMessage_2(msg, 3, NULL, NULL);

        sprintf(msg, "[INFO] %d batch files initialized", batchnum);
        QueueUIMessage_2(msg, 3, NULL, NULL);

        not_initialized = false;
      }
    } break;
  }
}

//NOTE(adm244): obse calls us in here to get the information about our plugin
extern "C" bool OBSEPlugin_Query(const OBSEInterface * obse, PluginInfo * info)
{
  _MESSAGE("OBSEPlugin_Query");

  info->infoVersion = PluginInfo::kInfoVersion;
  info->name = SCRIPTNAME;
  info->version = 1;

  if(!obse->isEditor) {
    if(obse->obseVersion < OBSE_VERSION_INTEGER) {
      _ERROR("OBSE version too old (got %08X expected at least %08X)", obse->obseVersion, OBSE_VERSION_INTEGER);
      return(false);
    }

#if OBLIVION
    if(obse->oblivionVersion != OBLIVION_VERSION) {
      _ERROR("incorrect Oblivion version (got %08X need %08X)", obse->oblivionVersion, OBLIVION_VERSION);
      return(false);
    }
#endif

    g_ConsoleInterface = (OBSEConsoleInterface *)obse->QueryInterface(kInterface_Console);
    if(!g_ConsoleInterface){
      _ERROR("console interface not found");
      return(false);
    }

    if(g_ConsoleInterface->version < OBSEConsoleInterface::kVersion){
      _ERROR("incorrect console interface version found (got %08X need %08X)", g_ConsoleInterface->version, OBSEConsoleInterface::kVersion);
      _ERROR("console interface invalid version");
      return(false);
    }

    g_ScriptInterface = (OBSEScriptInterface *)obse->QueryInterface(kInterface_Script);
    if(!g_ScriptInterface){
      _ERROR("script interface not found");
      return(false);
    }

    g_ArrayVarInterface = (OBSEArrayVarInterface *)obse->QueryInterface(kInterface_ArrayVar);
    if(!g_ArrayVarInterface){
      _ERROR("arrayvar interface not found");
      return(false);
    }
  }

  _MESSAGE("Success");
  return(true);
}

//NOTE(adm244): obse calls us in here so we can perform some initializations
extern "C" bool OBSEPlugin_Load(const OBSEInterface * obse)
{
  _MESSAGE("OBSEPlugin_Load");

  g_pluginHandle = obse->GetPluginHandle();

  OBSEMessagingInterface *msgIntfc = (OBSEMessagingInterface *)obse->QueryInterface(kInterface_Messaging);
  msgIntfc->RegisterListener(g_pluginHandle, "OBSE", MessageHandler);

  if( !main_init() ){
    return(false);
  }

  _MESSAGE("Patching Oblivion...");
  //NOTE(adm244): patching in the oblivion main loop right after obse
  SafeWrite8(mainloop_hook_patch_address - 1, 0x90); // nop (for debugger friendliness)
  WriteRelJump(mainloop_hook_patch_address, (UInt32)&mainloop_hook);

  _MESSAGE("Success");
  return(true);
}

//NOTE(adm244): dll entry point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved)
{
  switch (fdwReason) {
    case DLL_PROCESS_ATTACH: {
      g_hModule = hModule;
      g_TimerQueue = CreateTimerQueue();
    } break;
    case DLL_PROCESS_DETACH: {
    } break;
  }
  return(TRUE);
}
