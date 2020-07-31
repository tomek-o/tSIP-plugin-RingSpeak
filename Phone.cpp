//---------------------------------------------------------------------------

#define _EXPORTING
#include "..\tSIP\tSIP\phone\Phone.h"
#include "..\tSIP\tSIP\phone\PhoneSettings.h"
#include "..\tSIP\tSIP\phone\PhoneCapabilities.h"
#include "Log.h"
#include <assert.h>
#include <algorithm>	// needed by Utils::in_group
#include "Utils.h"
#include "sapi.h"
#include <string>


//---------------------------------------------------------------------------

#define PROMPT "RingSpeak: "

static const struct S_PHONE_DLL_INTERFACE dll_interface =
{DLL_INTERFACE_MAJOR_VERSION, DLL_INTERFACE_MINOR_VERSION};

// callback ptrs
CALLBACK_LOG lpLogFn = NULL;
CALLBACK_CONNECT lpConnectFn = NULL;
CALLBACK_KEY lpKeyFn = NULL;
CALLBACK_GET_NUMBER_DESCRIPTION lpGetNumberDescriptionFn = NULL;


void *callbackCookie;	///< used by upper class to distinguish library instances when receiving callbacks

namespace {
	ISpVoice* pVoice = NULL;
	std::string callDisplay;
	int callState = 0;
}


extern "C" __declspec(dllexport) void GetPhoneInterfaceDescription(struct S_PHONE_DLL_INTERFACE* interf) {
    interf->majorVersion = dll_interface.majorVersion;
    interf->minorVersion = dll_interface.minorVersion;
}

void Log(const char* txt) {
    if (lpLogFn)
        lpLogFn(callbackCookie, (char*)txt);
}

void Connect(int state, char *szMsgText) {
    if (lpConnectFn)
        lpConnectFn(callbackCookie, state, szMsgText);
}

void Key(int keyCode, int state) {
    if (lpKeyFn)
        lpKeyFn(callbackCookie, keyCode, state);
}

void SetCallbacks(void *cookie, CALLBACK_LOG lpLog, CALLBACK_CONNECT lpConnect, CALLBACK_KEY lpKey) {
    assert(cookie && lpLog && lpConnect && lpKey);
    lpLogFn = lpLog;
    lpConnectFn = lpConnect;
    lpKeyFn = lpKey;
    callbackCookie = cookie;
    lpLogFn(callbackCookie, (char*)"Phone DLL for EX03 loaded\n");
}

void GetPhoneCapabilities(struct S_PHONE_CAPABILITIES **caps) {
    static struct S_PHONE_CAPABILITIES capabilities = {
        0
    };
    *caps = &capabilities;
}

void ShowSettings(HANDLE parent) {
    MessageBox((HWND)parent, "No additional settings.", "Device DLL", MB_ICONINFORMATION);
}

int Connect(void) {
	// Create SAPI voice
	if(CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&pVoice)){
		Log(PROMPT"Couldn't create voice.\n");
		CoUninitialize();
		return 1;
	}
	Log(PROMPT"Voice created.\n");
    return 0;
}

int Disconnect(void) {
    //RingThreadStop();   // shouldn't be needed
    if (pVoice) {
        // Release voice
        pVoice->lpVtbl->Release(pVoice);
        Log(PROMPT"Voice released.");
        pVoice = NULL;
    }
	return 0;
}

static bool bSettingsReaded = false;

static int GetDefaultSettings(struct S_PHONE_SETTINGS* settings) {
    settings->ring = 1;

    bSettingsReaded = true;
    return 0;
}

int GetPhoneSettings(struct S_PHONE_SETTINGS* settings) {
    //settings->iTriggerSrcChannel = 0;

    std::string path = Utils::GetDllPath();
    path = Utils::ReplaceFileExtension(path, ".cfg");
    if (path == "")
        return GetDefaultSettings(settings);


    GetDefaultSettings(settings);


    //int mode = root.get("TriggerMode", TRIGGER_MODE_AUTO).asInt();
    settings->ring = true;//root.get("ring", settings->ring).asInt();


    bSettingsReaded = true;
    return 0;
}

int SavePhoneSettings(struct S_PHONE_SETTINGS* settings) {

    return 0;
}

int SetRegistrationState(int state) {
    return 0;
}

bool connected = false;
bool exited = true;

DWORD WINAPI RingThreadProc(LPVOID data) {
	// Initialize COM
	if(CoInitializeEx(NULL, COINIT_MULTITHREADED)) {
		Log(PROMPT"Couldn't initialize COM.\n");
		return 1;
    }

    // wait for SetCallState()
    for (int i=0; i<10; i++) {
        if (callState != 0)
            break;
        Sleep(100);
    }

    while (connected && pVoice) {
        // Speak with voice
        wchar_t text[1024];;
        const unsigned text_size = sizeof(text) / sizeof(*text);
        Log("TTS SAPI Speaking...\n");
        if (callDisplay == "") {
            // Comma then a space a few times should insert bit of silence
            wcscpy(text, L"Incoming call,   ");
        } else {
            std::wstring widestr;
            if (lpGetNumberDescriptionFn) {
                char description[128];
                if (lpGetNumberDescriptionFn(callbackCookie, callDisplay.c_str(), description, sizeof(description)) == 0) {
                    if (description[0] != '\0') {
                        widestr = std::wstring(description, description + strlen(description));
                    }
                }
            }
            if (widestr.empty()) {
                if (callDisplay.length() < 5) {
                    widestr = std::wstring(callDisplay.begin(), callDisplay.end());
                }
            }
            if (!widestr.empty()) {
                memset(text, 0, sizeof(text));
                // Comma then a space a few times should insert bit of silence
                snwprintf(text, text_size-1, L"Incoming call from %s,   ", widestr.c_str());
            } else {
                wcscpy(text, L"Incoming call,   ");
            }
        }

        int rc = pVoice->lpVtbl->Speak(pVoice, text, /*SPF_ASYNC|*/SPF_DEFAULT, NULL);
		if(rc){
			LOG("Couldn't speak with voice, rc = %d = 0x%08X.\n", rc, rc);
			pVoice->lpVtbl->Release(pVoice);
			pVoice = NULL;
            exited = true;
            // Finish COM
            CoUninitialize();
            Log(PROMPT"COM uninitialized.");
			return 1;
		}
        Sleep(100);
    }

	// Finish COM
	CoUninitialize();
	Log(PROMPT"COM uninitialized.");

    exited = true;
    return 0;
}


int RingThreadStart(void) {
    DWORD dwtid;
    exited = false;
    connected = true;
    HANDLE RingThread = CreateThread(NULL, 0, RingThreadProc, /*this*/NULL, 0, &dwtid);
    if (RingThread == NULL) {
        Log("Failed to create ring thread.");
        connected = false;
        exited = true;
    } else {
        Log("Ring thread created.\n");
    }
    return 0;
}

int RingThreadStop(void) {
    connected = false;
    while (!exited) {
        Sleep(50);
    }
    return 0;
}

int Ring(int state) {
    if (state) {
        if (connected == false) {
            RingThreadStart();
        }
    } else {
        RingThreadStop();
    }
    return 0;
}

int SetCallState(int state, const char* display) {
    callState = state;
	callDisplay = display;
	return 0;
}

void SetGetNumberDescriptionCallback(CALLBACK_GET_NUMBER_DESCRIPTION lpGetNumberDescription) {
	lpGetNumberDescriptionFn = lpGetNumberDescription;
}
