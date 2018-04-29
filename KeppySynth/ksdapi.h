// KSDAPI calls

void keepstreamsalive(int& opend) {
	BASS_ChannelIsActive(KSStream);
	if (BASS_ErrorGetCode() == 5 || livechange == 1) {
		PrintToConsole(FOREGROUND_RED, 1, "Restarting audio stream...");
		CloseThreads();
		LoadSettings(TRUE);
		if (!com_initialized) { if (!FAILED(CoInitialize(NULL))) com_initialized = TRUE; }
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
		if (InitializeBASS(FALSE)) {
			SetUpStream();
			LoadSoundFontsToStream();
			opend = CreateThreads(TRUE);
		}
		streaminitialized = TRUE;
	}
}

DWORD WINAPI threadfunc(LPVOID lpV) {
	try {
		if (BannedSystemProcess() == TRUE) {
			_endthread();
			return 0;
		}
		else {
			int opend = 0;
			while (opend == 0) {
				LoadSettings(FALSE);
				allocate_memory();
				load_bassfuncs();
				if (!com_initialized) {
					if (FAILED(CoInitialize(NULL))) continue;
					com_initialized = TRUE;
				}
				SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
				if (InitializeBASS(FALSE)) {
					SetUpStream();
					LoadSoundFontsToStream();
					opend = CreateThreads(TRUE);
				}
				streaminitialized = TRUE;
			}
			PrintToConsole(FOREGROUND_RED, 1, "Checking for settings changes or hotkeys...");
			while (stop_rtthread == FALSE) {
				start1 = TimeNow();
				keepstreamsalive(opend);
				LoadCustomInstruments();
				CheckVolume();
				ParseDebugData();
				Sleep(10);
			}
			stop_rtthread = FALSE;
			FreeUpLibraries();
			PrintToConsole(FOREGROUND_RED, 1, "Closing main thread...");
			ExitThread(0);
			return 0;
		}
	}
	catch (...) {
		CrashMessage(L"DrvMainThread");
		ExitThread(0);
		throw;
		return 0;
	}
}

void DoCallback(int clientNum, DWORD msg, DWORD_PTR param1, DWORD_PTR param2) {
	struct Driver_Client *client = &drivers[0].clients[clientNum];
	DriverCallback(client->callback, client->flags, drivers[0].hdrvr, msg, client->instance, param1, param2);
}

void DoStartClient() {
	if (modm_closed == TRUE) {
		HKEY hKey;
		long lResult;
		DWORD dwType = REG_DWORD;
		DWORD dwSize = sizeof(DWORD);
		int One = 0;
		lResult = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Keppy's Synthesizer", 0, KEY_ALL_ACCESS, &hKey);
		RegQueryValueEx(hKey, L"driverprio", NULL, &dwType, (LPBYTE)&driverprio, &dwSize);
		RegCloseKey(hKey);
		lResult = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Keppy's Synthesizer\\Settings", 0, KEY_ALL_ACCESS, &hKey);
		RegQueryValueEx(hKey, L"improveperf", NULL, &dwType, (LPBYTE)&improveperf, &dwSize);
		RegCloseKey(hKey);

		AppName();
		StartDebugPipe(FALSE);

		InitializeCriticalSection(&midiparsing);
		DWORD result;
		processPriority = GetPriorityClass(GetCurrentProcess());
		SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
		load_sfevent = CreateEvent(
			NULL,               // default security attributes
			TRUE,               // manual-reset event
			FALSE,              // initial state is nonsignaled
			TEXT("SoundFontEvent")  // object name
		);
		hCalcThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)threadfunc, NULL, 0, (LPDWORD)thrdaddrC);
		SetThreadPriority(hCalcThread, prioval[driverprio]);
		result = WaitForSingleObject(load_sfevent, INFINITE);
		if (result == WAIT_OBJECT_0)
		{
			CloseHandle(load_sfevent);
		}
		modm_closed = FALSE;
	}
}

void DoStopClient() {
	if (modm_closed == FALSE) {
		stop_thread = TRUE;
		stop_rtthread = TRUE;
		WaitForSingleObject(hCalcThread, INFINITE);
		CloseHandle(hCalcThread);
		modm_closed = TRUE;
		SetPriorityClass(GetCurrentProcess(), processPriority);
	}
	DeleteCriticalSection(&midiparsing);
}

void DoResetClient() {
	reset_synth = 1;
	ResetSynth(0);
}

char const* WINAPI ReturnKSDAPIVer()
{
	return "v1.3 (Release)";
}

BOOL WINAPI IsKSDAPIAvailable() 
{
	HKEY hKey;
	long lResult;
	DWORD dwType = REG_DWORD;
	DWORD dwSize = sizeof(DWORD);
	lResult = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Keppy's Synthesizer\\Settings", 0, KEY_READ, &hKey);
	RegQueryValueEx(hKey, L"allowksdapi", NULL, &dwType, (LPBYTE)&ksdirectenabled, &dwSize);
	RegCloseKey(hKey);

	return ksdirectenabled;
}

void InitializeKSStream() {
	DoStartClient();
}

void TerminateKSStream() {
	DoStopClient();
}

void ResetKSStream() {
	ResetSynth(0);
}

MMRESULT WINAPI SendDirectData(DWORD dwMsg)
{
	if (streaminitialized) 
		return ParseData(evbpoint, MODM_DATA, 0, dwMsg, NULL, NULL, NULL);
	else 
		return MMSYSERR_NOERROR;
}

MMRESULT WINAPI SendDirectDataNoBuf(DWORD dwMsg)
{
	try {
		if (streaminitialized) SendToBASSMIDI(dwMsg);
		return MMSYSERR_NOERROR;
	}
	catch (...) { return MMSYSERR_INVALPARAM; }
}

MMRESULT WINAPI SendDirectLongData(MIDIHDR* IIMidiHdr)
{
	int exlen = 0;
	unsigned char *sysexbuffer = NULL;
	DWORD_PTR dwUser = IIMidiHdr->dwUser;

	if (!(IIMidiHdr->dwFlags & MHDR_PREPARED)) return MIDIERR_UNPREPARED;
	IIMidiHdr->dwFlags &= ~MHDR_DONE;
	IIMidiHdr->dwFlags |= MHDR_INQUEUE;
	exlen = (int)IIMidiHdr->dwBufferLength;

	if (NULL == (sysexbuffer = (unsigned char *)malloc(exlen * sizeof(char)))) return MMSYSERR_NOMEM;
	else memcpy(sysexbuffer, IIMidiHdr->lpData, exlen);

	IIMidiHdr->dwFlags &= ~MHDR_INQUEUE;
	IIMidiHdr->dwFlags |= MHDR_DONE;
	DoCallback(static_cast<LONG>(dwUser), MOM_DONE, (DWORD_PTR)IIMidiHdr, 0);
}