/* 
 * Copyright (C) 2011-2016 MicroSIP (http://www.microsip.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */

#pragma once

#include "global.h"

struct Account {
	CString label;
	CString server;
	CString proxy;
	CString username;
	CString domain;
	int port;
	CString authID;
	CString password;
	int rememberPassword;
	CString displayName;
	CString voicemailNumber;
	CString srtp;
	CString transport;
	CString publicAddr;
	int publish;
	int ice;
	int allowRewrite;
	int disableSessionTimer;
};

struct AccountSettings {
	int accountId;
	Account account;
	CString userAgent;
	int DTMFMethod;
	int autoAnswer;
	CString denyIncoming;
	CString usersDirectory;
	CString stun;
	int enableSTUN;
	int singleMode;
	int enableLocalAccount;
	int crashReport;
	int enableLog;
	int randomAnswerBox;
	CString ringingSound;
	CString audioRingDevice;
	CString audioOutputDevice;
	CString audioInputDevice;
	int micAmplification;
	int swLevelAdjustment;
	CString audioCodecs;
	int sourcePort;
	int vad;
	int ec;
	int forceCodec;
	CString videoCaptureDevice;
	CString videoCodec;
	int disableH264;
	CString bitrateH264;	
	int disableH263;
	CString bitrateH263;
	int disableVP8;
	CString bitrateVP8;
	int localDTMF;

	CString portKnockerHost;
	CString portKnockerPorts;

	CString lastCallNumber;
	bool lastCallHasVideo;

	CString updatesInterval;

	int activeTab;
	int alwaysOnTop;

	int mainX;
	int mainY;
	int mainW;
	int mainH;
	int noResize;

	int messagesX;
	int messagesY;
	int messagesW;
	int messagesH;

	int ringinX;
	int ringinY;

	int callsWidth0;
	int callsWidth1;
	int callsWidth2;
	int callsWidth3;
	int callsWidth4;

	int contactsWidth0;

	int volumeOutput;
	int volumeInput;
	
	CString iniFile;
	CString logFile;
	CString exeFile;
	CString pathRoaming;
	CString pathLocal;
	CString pathExe;

	int checkUpdatesTime;

	int hidden;
	int silent;
	
	int autoHangUpTime;
	int maxConcurrentCalls;

	CString cmdCallStart;
	CString cmdCallEnd;
	CString cmdIncomingCall;
	CString cmdCallAnswer;
	int enableShortcuts;
	int shortcutsBottom;
	AccountSettings();
	void Init();
	bool AccountLoad(int id, Account *account);
	void AccountSave(int id, Account *account);
	void AccountDelete(int id);
	void SettingsSave();
};

extern AccountSettings accountSettings;
extern bool firstRun;
extern bool pj_ready;

CString ShortcutEncode(Shortcut *pShortcut);
void ShortcutDecode(CString str, Shortcut *pShortcut);
void ShortcutsLoad();
void ShortcutsSave();
extern CArray<Shortcut,Shortcut> shortcuts;
