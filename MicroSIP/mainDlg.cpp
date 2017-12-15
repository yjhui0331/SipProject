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

#include "stdafx.h"
#include "microsip.h"
#include "mainDlg.h"
#include "Mmsystem.h"
#include "settings.h"
#include "global.h"
#include "ModelessMessageBox.h"
#include "utf.h"
#include "langpack.h"
#include "jumplist.h"
#include "atlenc.h"

#include <io.h>
#include <afxinet.h>
#include <ws2tcpip.h>
#include <Dbt.h>
#include <Strsafe.h>
#include <locale.h> 
#include <Wtsapi32.h>
#include <afxsock.h>
#include <atlrx.h>

using namespace MSIP;

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CmainDlg *mainDlg;

static UINT BASED_CODE indicators[] =
{
	IDS_STATUSBAR
};

static bool timerContactBlinkState = false;

static CString gethostbyaddrThreadResult;
static DWORD WINAPI gethostbyaddrThread( LPVOID lpParam ) 
{
	CString *addr = (CString *)lpParam;
	gethostbyaddrThreadResult = *addr;
	struct hostent *he = NULL;
	struct in_addr inaddr;
	inaddr.S_un.S_addr = inet_addr(CStringA(*addr));
	if (inaddr.S_un.S_addr != INADDR_NONE && inaddr.S_un.S_addr != INADDR_ANY) {
		he = gethostbyaddr((char *) &inaddr, 4, AF_INET);
		if (he) {
			gethostbyaddrThreadResult = he->h_name;
		}
	}
	delete addr;
	return 0;
}

static void on_reg_state2(pjsua_acc_id acc_id,  pjsua_reg_info *info)
{
	if (!IsWindow(mainDlg->m_hWnd))	{
		return;
	}
	CString *str = NULL;
	PostMessage(mainDlg->m_hWnd, UM_ON_REG_STATE2, (WPARAM) info->cbparam->code, (LPARAM) str);
}

LRESULT CmainDlg::onRegState2(WPARAM wParam,LPARAM lParam)
{
	int code = wParam;

	if (code == 200) {
		if (accountSettings.usersDirectory.Find(_T("%s"))!=-1 || accountSettings.usersDirectory.Find(_T("{"))!=-1) {
			UsersDirectoryLoad();
		}
	}

	UpdateWindowText(_T(""), IDI_DEFAULT, true);
	
	return 0;
}

/* Callback from timer when the maximum call duration has been
 * exceeded.
 */
static void call_timeout_callback(pj_timer_heap_t *timer_heap,
				  struct pj_timer_entry *entry)
{
    pjsua_call_id call_id = entry->id;
    pjsua_msg_data msg_data_;
    pjsip_generic_string_hdr warn;
    pj_str_t hname = pj_str("Warning");
    pj_str_t hvalue = pj_str("399 localhost \"Call duration exceeded\"");

    PJ_UNUSED_ARG(timer_heap);

    if (call_id == PJSUA_INVALID_ID) {
	PJ_LOG(1,(THIS_FILE, "Invalid call ID in timer callback"));
	return;
    }
    
    /* Add warning header */
    pjsua_msg_data_init(&msg_data_);
    pjsip_generic_string_hdr_init2(&warn, &hname, &hvalue);
    pj_list_push_back(&msg_data_.hdr_list, &warn);

    /* Call duration has been exceeded; disconnect the call */
    PJ_LOG(3,(THIS_FILE, "Duration (%d seconds) has been exceeded "
			 "for call %d, disconnecting the call",
			 accountSettings.autoHangUpTime, call_id));
    entry->id = PJSUA_INVALID_ID;
    pjsua_call_hangup(call_id, 200, NULL, &msg_data_);
}

static void on_call_state(pjsua_call_id call_id, pjsip_event *e)
{
	if (!IsWindow(mainDlg->m_hWnd))	{
		return;
	}
	pjsua_call_info *call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	// reset user_data after call transfer
	call_user_data *user_data = (call_user_data *) pjsua_call_get_user_data(call_info->id);
	if (user_data && user_data->call_id != PJSUA_INVALID_ID && user_data->call_id != call_info->id) {
		pjsua_call_set_user_data(call_info->id, NULL);
	}

	CString *str = new CString();
	CString adder;

	if (call_info->state!=PJSIP_INV_STATE_DISCONNECTED && call_info->state!=PJSIP_INV_STATE_CONNECTING && call_info->remote_contact.slen>0) {
		SIPURI contactURI;
		ParseSIPURI(PjToStr(&call_info->remote_contact,TRUE),&contactURI);
		CString contactDomain = RemovePort(contactURI.domain);
		struct hostent *he = NULL;
		if (IsIP(contactDomain)) {
			HANDLE hThread;
			CString *addr = new CString;
			*addr = contactDomain;
			hThread = CreateThread(NULL,0, gethostbyaddrThread, addr, 0, NULL);
			if (WaitForSingleObject(hThread, 500)==0) {
				contactDomain = gethostbyaddrThreadResult;
			}
		}
		adder.AppendFormat(_T("%s, "),contactDomain);
	}

	unsigned cnt = 0;
	unsigned cnt_srtp = 0;

	switch (call_info->state) {
	case PJSIP_INV_STATE_CALLING:
		str->Format(_T("%s..."),Translate(_T("Calling")));
		break;
	case PJSIP_INV_STATE_INCOMING:
		str->SetString(Translate(_T("Incoming Call")));
		break;
	case PJSIP_INV_STATE_EARLY:
		str->SetString(Translate( PjToStr(&call_info->last_status_text).GetBuffer() ));
		break;
	case PJSIP_INV_STATE_CONNECTING:
		str->Format(_T("%s..."),Translate(_T("Connecting")));
		break;
	case PJSIP_INV_STATE_CONFIRMED:
		if (!user_data) {
			user_data = new call_user_data(call_info->id);
			pjsua_call_set_user_data(call_info->id,user_data);
		}
		if (accountSettings.autoHangUpTime>0) {
			/* Schedule timer to hangup call after the specified duration */
			pj_time_val delay;
			user_data->auto_hangup_timer.id = call_info->id;
			user_data->auto_hangup_timer.cb = &call_timeout_callback;
			delay.sec = accountSettings.autoHangUpTime;
			delay.msec = 0;
			pjsua_schedule_timer(&user_data->auto_hangup_timer, &delay);
		}
		str->SetString(Translate(_T("Connected")));
		for (unsigned i=0;i<call_info->media_cnt;i++) {
			if (call_info->media[i].dir != PJMEDIA_DIR_NONE &&
				(call_info->media[i].type == PJMEDIA_TYPE_AUDIO || call_info->media[i].type == PJMEDIA_TYPE_VIDEO)) {
				cnt++;
				pjsua_stream_info psi;
				if (pjsua_call_get_stream_info(call_info->id, call_info->media[i].index, &psi) == PJ_SUCCESS) {
					if (call_info->media[i].type == PJMEDIA_TYPE_AUDIO) {
						adder.AppendFormat(_T("%s@%dkHz %dkbit/s%s, "),
							PjToStr (&psi.info.aud.fmt.encoding_name),psi.info.aud.fmt.clock_rate/1000, 
							psi.info.aud.param->info.avg_bps/1000,
							psi.info.aud.fmt.channel_cnt==2?_T(" Stereo"):_T("")
							);
					} else {
						adder.AppendFormat(_T("%s %dkbit/s, "),
							PjToStr (&psi.info.vid.codec_info.encoding_name),
							psi.info.vid.codec_param->enc_fmt.det.vid.max_bps/1000
							);
					}
				}
				pjmedia_transport_info t;
				if (pjsua_call_get_med_transport_info (call_info->id, call_info->media[i].index, &t) == PJ_SUCCESS) {
					for (unsigned j=0;j<t.specific_info_cnt;j++) {
						if (t.spc_info[j].buffer[0]) {
							switch(t.spc_info[j].type) {
	case PJMEDIA_TRANSPORT_TYPE_SRTP:
		adder.Append(_T("SRTP, "));
		cnt_srtp++;
		break;
	case PJMEDIA_TRANSPORT_TYPE_ICE:
		adder.Append(_T("ICE, "));
		break;
							}
						}
					}
				}
			}
		}
		if (cnt_srtp && cnt == cnt_srtp) {
			user_data->srtp = MSIP_SRTP;
		} else {
			user_data->srtp = MSIP_SRTP_DISABLED;
		}
		break;
	case PJSIP_INV_STATE_DISCONNECTED:
		call_deinit_tonegen(call_info->id);
		//--
		msip_conference_leave(call_info);
		//--
		// delete user data
		if (user_data) {
			/* Cancel duration timer, if any */
			if (user_data->auto_hangup_timer.id != PJSUA_INVALID_ID) {
				pjsua_cancel_timer(&user_data->auto_hangup_timer);
				user_data->auto_hangup_timer.id = PJSUA_INVALID_ID;
			}
			pjsua_call_set_user_data(call_info->id, NULL);
			delete user_data;
		}
		// --
		if (call_info->last_status == 200) {
			str->SetString(Translate(_T("Call Ended")));
		} else {
			str->SetString(PjToStr(&call_info->last_status_text).GetBuffer());
			if (*str == _T("Decline")) {
				str->SetString(_T("Declined"));
			} else if (str->Find(_T("(PJ_ERESOLVE)"))!=-1) {
				str->SetString(_T("Cannot get IP address of the called host."));
			}
			str->SetString(Translate(str->GetBuffer()));
		}
		break;
	}

	if (!str->IsEmpty() && !adder.IsEmpty()) {
		str->AppendFormat(_T(" (%s)"), adder.Left(adder.GetLength()-2));
	}

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_STATE, (WPARAM) call_info, (LPARAM) str);
}

LRESULT CmainDlg::onCallState(WPARAM wParam,LPARAM lParam)
{
	pjsua_call_info *call_info = (pjsua_call_info *) wParam;
	CString *str = (CString *) lParam;

	CString number = PjToStr(&call_info->remote_info, TRUE);
	SIPURI sipuri;
	ParseSIPURI(number,&sipuri);
	if (call_info->state == PJSIP_INV_STATE_CALLING) {
		msip_call_unhold(call_info);
	} else if (call_info->state == PJSIP_INV_STATE_CONNECTING) {
		msip_call_unhold(call_info);
	} else if (call_info->state == PJSIP_INV_STATE_CONFIRMED) {
		if (! callTimer) {
			OnTimerCall();
			callTimer = SetTimer(IDT_TIMER_CALL,1000,NULL);
		}
		if (!accountSettings.cmdCallStart.IsEmpty() ) {
			ShellExecute(NULL, NULL, accountSettings.cmdCallStart, sipuri.user, NULL, SW_HIDE);
		}
		call_user_data *user_data = (call_user_data *) pjsua_call_get_user_data(call_info->id);
		if (!user_data->commands.IsEmpty()) {
			pageDialer->DTMF(user_data->commands);
		}
	} else if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		destroyDTMFPlayer(NULL,NULL,NULL,NULL);
		if (call_info->last_status == 200) {
			onPlayerPlay(MSIP_SOUND_HANGUP, 0);
		} else {
			if (accountSettings.singleMode && call_info->last_status != 487 &&  (call_info->last_status != 603 || call_info->role == PJSIP_ROLE_UAC)) {
				BaloonPopup(*str, *str, NIIF_INFO);
			}
		}
		if (call_info->last_status != 486) {
			if (!accountSettings.cmdCallEnd.IsEmpty() ) {
				ShellExecute(NULL, NULL, accountSettings.cmdCallEnd, sipuri.user, NULL, SW_HIDE);
			}
		}
	}

	if (!accountSettings.singleMode) {
		if (call_info->state!=PJSIP_INV_STATE_CONFIRMED) {
			if (call_info->state!=PJSIP_INV_STATE_DISCONNECTED) {
				UpdateWindowText(*str, call_info->acc_id == account && mainDlg->iconStatusbar==IDI_SECURE ? IDI_SECURE : IDI_ONLINE);
			} else {
				UpdateWindowText(_T("-"));
			}
		}
	}

	if (call_info->role==PJSIP_ROLE_UAC && accountSettings.localDTMF) {
		if (call_info->last_status == 180 && !call_info->media_cnt) {
			if (toneCalls.IsEmpty()) {
					OnTimer(IDT_TIMER_TONE);
					SetTimer(IDT_TIMER_TONE,4500,NULL);
					toneCalls.AddTail(call_info->id);
			} else if (toneCalls.Find(call_info->id)==NULL) {
				toneCalls.AddTail(call_info->id);
			}
		} else {
			POSITION position = toneCalls.Find(call_info->id);
			if (position!=NULL) {
				toneCalls.RemoveAt(position);
				if (toneCalls.IsEmpty()) {
					KillTimer(IDT_TIMER_TONE);
					PlayerStop();
				}
			}
		}
	}

	if (!messagesDlg) {
		return 0;
	}

	bool doNotShowMessagesWindow =
		call_info->state == PJSIP_INV_STATE_INCOMING ||
		call_info->state == PJSIP_INV_STATE_EARLY ||
		call_info->state == PJSIP_INV_STATE_DISCONNECTED;
	MessagesContact* messagesContact = messagesDlg->AddTab(number, _T(""),
		(!accountSettings.singleMode &&
				(call_info->state == PJSIP_INV_STATE_CONFIRMED
				|| call_info->state == PJSIP_INV_STATE_CONNECTING)
		)
		||
		(accountSettings.singleMode
		&&
		(
		(call_info->role==PJSIP_ROLE_UAC && call_info->state != PJSIP_INV_STATE_DISCONNECTED)
		||
				(call_info->role==PJSIP_ROLE_UAS &&
				(call_info->state == PJSIP_INV_STATE_CONFIRMED
				|| call_info->state == PJSIP_INV_STATE_CONNECTING)
				)
		))
		?TRUE:FALSE,
		call_info, accountSettings.singleMode || doNotShowMessagesWindow, call_info->state == PJSIP_INV_STATE_DISCONNECTED
	);

	if (messagesContact) {
		if ( call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
			messagesContact->mediaStatus = PJSUA_CALL_MEDIA_ERROR;
		}
		if (call_info->role==PJSIP_ROLE_UAS) {
			if ( call_info->state == PJSIP_INV_STATE_CONFIRMED) {
				pageCalls->Add(call_info->call_id, messagesContact->number + messagesContact->numberParameters, messagesContact->name, MSIP_CALL_IN);
			} else if ( call_info->state == PJSIP_INV_STATE_INCOMING || call_info->state == PJSIP_INV_STATE_EARLY) {
				pageCalls->Add(call_info->call_id, messagesContact->number + messagesContact->numberParameters, messagesContact->name , MSIP_CALL_MISS);
			}
		} else {
			if ( call_info->state == PJSIP_INV_STATE_CALLING) {
				pageCalls->Add(call_info->call_id, messagesContact->number + messagesContact->numberParameters, messagesContact->name, MSIP_CALL_OUT);
			}
		}
	}
	if ( call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		pageCalls->SetDuration(call_info->call_id, call_info->connect_duration.sec);
		if (call_info->last_status != 200) {
			pageCalls->SetInfo(call_info->call_id, *str);
		}
	}

	if (call_info->role==PJSIP_ROLE_UAS && call_info->state == PJSIP_INV_STATE_CONFIRMED)	{
		if (!accountSettings.cmdCallAnswer.IsEmpty() ) {
			ShellExecute(NULL, NULL, accountSettings.cmdCallAnswer, sipuri.user, NULL, SW_HIDE);
		}
	}

	if (accountSettings.singleMode)	{
		if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
			MessagesContact *messagesContactSelected = messagesDlg->GetMessageContact();
			if (!messagesContactSelected || messagesContactSelected->callId==call_info->id || messagesContactSelected->callId==-1) {
				pageDialer->Clear(false);
				pageDialer->UpdateCallButton(FALSE, 0);
			}
			UpdateWindowText(_T("-"));
		} else {
			if (call_info->state!=PJSIP_INV_STATE_CONFIRMED) {
				UpdateWindowText(*str, call_info->acc_id == account && mainDlg->iconStatusbar==IDI_SECURE ? IDI_SECURE : IDI_ONLINE);
			}
			int tabN = 0;
			GotoTab(tabN);
			messagesDlg->OnChangeTab(call_info);
		}
	} else {
		if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
			pageDialer->Clear();
		}
	}

	if (messagesContact && !str->IsEmpty())	{
		messagesDlg->AddMessage(messagesContact, *str, MSIP_MESSAGE_TYPE_SYSTEM,
			call_info->state == PJSIP_INV_STATE_INCOMING || call_info->state == PJSIP_INV_STATE_EARLY
			);
	}

	if (call_info->state == PJSIP_INV_STATE_DISCONNECTED) {
		messagesDlg->OnEndCall(call_info);
		if (!pjsua_call_get_count()) {
#ifdef _GLOBAL_VIDEO
			if (previewWin) {
				previewWin->PostMessage(WM_CLOSE, NULL, NULL);
			}
#endif
		}
	}

	if (call_info->role==PJSIP_ROLE_UAS) { 
		if (call_info->state == PJSIP_INV_STATE_DISCONNECTED || call_info->state == PJSIP_INV_STATE_CONFIRMED) {
			int count = ringinDlgs.GetCount();
			if (!count) {
				if (call_info->state == PJSIP_INV_STATE_CONFIRMED || (call_info->state == PJSIP_INV_STATE_DISCONNECTED && call_info->connect_duration.sec==0 && call_info->connect_duration.msec==0)) {
					PlayerStop();
				}
			} else {
				for (int i = 0; i < count; i++ ) {
					RinginDlg *ringinDlg = ringinDlgs.GetAt(i);
					if ( call_info->id == ringinDlg->call_id) {
						if (count==1) {
							PlayerStop();
						}
						ringinDlgs.RemoveAt(i);
						ringinDlg->DestroyWindow();
						break;
					}
				}
			}
		}
	}

	delete call_info;
	delete str;
	return 0;
}

static void on_call_media_state(pjsua_call_id call_id)
{
	pjsua_call_info *call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}
	if (call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE
		|| call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD
		) {
			msip_conference_join(call_info);
			pjsua_conf_connect(call_info->conf_slot, 0);
			pjsua_conf_connect(0, call_info->conf_slot);
			//--
			::SetTimer(mainDlg->pageDialer->m_hWnd,IDT_TIMER_VU_METER,100,NULL);
			//--
	} else {
		msip_conference_leave(call_info, true);
		pjsua_conf_disconnect(call_info->conf_slot, 0);
		pjsua_conf_disconnect(0, call_info->conf_slot);
		call_deinit_tonegen(call_id);
		//--
	}
	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_MEDIA_STATE, (WPARAM) call_info, 0);
}

LRESULT CmainDlg::onCallMediaState(WPARAM wParam,LPARAM lParam)
{
	pjsua_call_info *call_info = (pjsua_call_info *) wParam;

	messagesDlg->UpdateHoldButton(call_info);

	CString message;
	CString number = PjToStr(&call_info->remote_info, TRUE);

	MessagesContact* messagesContact = messagesDlg->AddTab(number, _T(""), FALSE, call_info, TRUE, TRUE);

	if (messagesContact) {
		if (call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD) {
			message = _T("Call on Remote Hold");
		}
		if (call_info->media_status == PJSUA_CALL_MEDIA_LOCAL_HOLD) {
			message = _T("Call on Local Hold");
		}
		if (call_info->media_status == PJSUA_CALL_MEDIA_NONE) {
			message = _T("Call on Hold");
		}
		if (messagesContact->mediaStatus != PJSUA_CALL_MEDIA_ERROR && messagesContact->mediaStatus != call_info->media_status && call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE) {
			message = _T("Call is Active");
		}
		if (!message.IsEmpty()) {
			messagesDlg->AddMessage(messagesContact, Translate(message.GetBuffer()), MSIP_MESSAGE_TYPE_SYSTEM, TRUE);
		}
		messagesContact->mediaStatus = call_info->media_status;
	}

	if (call_info->media_status == PJSUA_CALL_MEDIA_ACTIVE
		|| call_info->media_status == PJSUA_CALL_MEDIA_REMOTE_HOLD
		) {
			onRefreshLevels(0, 0);
	} else {
	}

	delete call_info;
	return 0;
}

static void on_incoming_call(pjsua_acc_id acc, pjsua_call_id call_id,
							 pjsip_rx_data *rdata)
{
	pjsua_call_info call_info;
	pjsua_call_get_info(call_id,&call_info);
	SIPURI sipuri;
	ParseSIPURI(PjToStr(&call_info.remote_info, TRUE), &sipuri);
	if (accountSettings.forceCodec) {
		pjsua_call *call;
		pjsip_dialog *dlg;
		pj_status_t status;
		status = acquire_call("on_incoming_call()", call_id, &call, &dlg);
		if (status == PJ_SUCCESS) {
			pjmedia_sdp_neg_set_prefer_remote_codec_order(call->inv->neg, PJ_FALSE);
			pjsip_dlg_dec_lock(dlg);
		}
	}

	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned calls_count = PJSUA_MAX_CALLS;
	unsigned calls_count_cmp = 0;
	if (pjsua_enum_calls ( call_ids, &calls_count)==PJ_SUCCESS)  {
		for (unsigned i = 0; i < calls_count; ++i) {
			pjsua_call_info call_info_curr;
			if (pjsua_call_get_info(call_ids[i], &call_info_curr) == PJ_SUCCESS) {
				SIPURI sipuri_curr;
				ParseSIPURI(PjToStr(&call_info_curr.remote_info, TRUE), &sipuri_curr);
				if (call_info_curr.id != call_info.id && 
					sipuri.user+_T("@")+sipuri.domain == sipuri_curr.user+_T("@")+sipuri_curr.domain
					) {
						// 486 Busy Here
						pjsua_call_hangup(call_info.id, 486, NULL, NULL);
						return;
				}
				if (call_info_curr.state!=PJSIP_INV_STATE_DISCONNECTED) {
					calls_count_cmp++;
				}
			}
		}
	}

	if (accountSettings.maxConcurrentCalls>0 && calls_count_cmp>accountSettings.maxConcurrentCalls) {
		// 486 Busy Here
		pjsua_call_hangup(call_info.id, 486, NULL, NULL);
		return;
	}

	if (IsWindow(mainDlg->m_hWnd)) {
		// -- create user_data
		call_user_data *user_data;
		user_data = (call_user_data *) pjsua_call_get_user_data(call_info.id);
		if (!user_data) {
			user_data = new call_user_data(call_info.id);
			pjsua_call_set_user_data(call_info.id,user_data);
		}
		// -- diversion
		const pj_str_t headerDiversion = {"Diversion",9};
		pjsip_generic_string_hdr *hsr = NULL;
		hsr = (pjsip_generic_string_hdr*) pjsip_msg_find_hdr_by_name ( rdata->msg_info.msg, &headerDiversion, NULL);
		if (hsr) {
			CString str = PjToStr(&hsr->hvalue, true);
			SIPURI sipuriDiversion;
			ParseSIPURI(str, &sipuriDiversion);
			user_data->diversion = !sipuriDiversion.user.IsEmpty()?sipuriDiversion.user:sipuriDiversion.domain;
		}
		// -- end diversion
		if (!mainDlg->callIdIncomingIgnore.IsEmpty() && mainDlg->callIdIncomingIgnore == PjToStr(&call_info.call_id)) {
			pjsua_call_answer(call_id, 487, NULL, NULL);
			return;
		}
		if (!accountSettings.denyIncoming.IsEmpty()) {
			bool reject = false;
			if (accountSettings.denyIncoming==_T("user")) {
				SIPURI sipuri_curr;
				ParseSIPURI(PjToStr(&call_info.local_info, TRUE), &sipuri_curr);
				if (sipuri_curr.user!=get_account_username()) {
					reject = true;
				}
			} else if (accountSettings.denyIncoming==_T("domain")) {
				SIPURI sipuri_curr;
				ParseSIPURI(PjToStr(&call_info.local_info, TRUE), &sipuri_curr);
				if (accountSettings.accountId) {
					if (sipuri_curr.domain!=accountSettings.account.domain) {
						reject = true;
					}
				}
			} else if (accountSettings.denyIncoming==_T("rdomain")) {
				if (accountSettings.accountId) {
					if (sipuri.domain!=accountSettings.account.domain) {
						reject = true;
					}
				}
			} else if (accountSettings.denyIncoming==_T("address")) {
				SIPURI sipuri_curr;
				ParseSIPURI(PjToStr(&call_info.local_info, TRUE), &sipuri_curr);
				if (sipuri_curr.user!=get_account_username() || (accountSettings.account.domain != _T("") && sipuri_curr.domain!=accountSettings.account.domain)) {
					reject = true;
				}
			} else if (accountSettings.denyIncoming==_T("all")) {
				reject = true;
			}
			if (reject) {
				mainDlg->PostMessage(UM_CALL_HANGUP, (WPARAM)call_id, NULL);
				return;
			}
		}

		if (!accountSettings.cmdIncomingCall.IsEmpty() ) {
			mainDlg->SendMessage(UM_SHELL_EXECUTE, (WPARAM)accountSettings.cmdIncomingCall.GetBuffer(), (LPARAM)sipuri.user.GetBuffer());
		}

		int autoAnswer = accountSettings.autoAnswer;
		if (autoAnswer==2) {
			pjsip_generic_string_hdr *hsr = NULL;
			const pj_str_t header = pj_str("X-AUTOANSWER");
			hsr = (pjsip_generic_string_hdr*) pjsip_msg_find_hdr_by_name ( rdata->msg_info.msg, &header, NULL);
			if (hsr) {
				CString autoAnswerValue = PjToStr(&hsr->hvalue, TRUE);
				autoAnswerValue.MakeLower();
				if (autoAnswerValue == _T("true") || autoAnswerValue == _T("1")) {
					autoAnswer = 1;
				}
			}
		}
		if (autoAnswer==2) {
			pjsip_generic_string_hdr *hsr = NULL;
			const pj_str_t header = pj_str("Call-Info");
			hsr = (pjsip_generic_string_hdr*) pjsip_msg_find_hdr_by_name ( rdata->msg_info.msg, &header, NULL);
			if (hsr) {
				CString callInfoValue = PjToStr(&hsr->hvalue, TRUE);
				callInfoValue.MakeLower();
				if (callInfoValue.Find(_T("auto answer"))!=-1) {
					autoAnswer = 1;
				} else {
					CAtlRegExp<> regex;
					REParseError parseStatus = regex.Parse(_T("answer-after={[0-9]+}"), true);
					if (parseStatus == REPARSE_ERROR_OK) {
						CAtlREMatchContext<> mc;
						if (regex.Match(callInfoValue, &mc) && mc.m_uNumGroups==1) {
							const CAtlREMatchContext<>::RECHAR* szStart = 0;
							const CAtlREMatchContext<>::RECHAR* szEnd = 0;
							mc.GetMatch(0, &szStart, &szEnd);
							ptrdiff_t nLength = szEnd - szStart;
							CStringA text(szStart, nLength);
							int autoAnswerTimeout = atoi(text);
							if (!autoAnswerTimeout) {
								autoAnswer = 1;
							} else if (autoAnswerTimeout>0) {
								if (mainDlg->autoAnswerCallId==PJSUA_INVALID_ID) {
									mainDlg->autoAnswerCallId = call_id;
									mainDlg->SetTimer(IDT_TIMER_AUTOANSWER,autoAnswerTimeout*1000,NULL);
								}
							}
						}
					}
				}
			}
		}
		BOOL playBeep = FALSE;
		if (autoAnswer!=1 || calls_count>1) {
			if (accountSettings.hidden) {
				mainDlg->PostMessage(UM_CALL_HANGUP, (WPARAM)call_id, NULL);
			} else {
				//--
				const pj_str_t headerUserAgent = {"User-Agent",10};
				pjsip_generic_string_hdr *hsr = NULL;
				hsr = (pjsip_generic_string_hdr*) pjsip_msg_find_hdr_by_name ( rdata->msg_info.msg, &headerUserAgent, NULL);
				if (hsr) {
					user_data->userAgent = PjToStr(&hsr->hvalue, true);
				}
				//--
				mainDlg->SendMessage(UM_CREATE_RINGING, (WPARAM)&call_info, (LPARAM)user_data);
				pjsua_call_answer(call_id, 180, NULL, NULL);
				if (call_get_count_noincoming()) {
					playBeep = TRUE;
				} else {
					if (!accountSettings.ringingSound.GetLength()) {
						mainDlg->PostMessage(UM_ON_PLAYER_PLAY,MSIP_SOUND_RINGIN,0);
					} else {
						mainDlg->PostMessage(UM_ON_PLAYER_PLAY,MSIP_SOUND_CUSTOM,(LPARAM)&accountSettings.ringingSound);
					}
				}
			}
		} else {
			mainDlg->AutoAnswer(call_id);
		}
		if (playBeep) {
			mainDlg->PostMessage(UM_ON_PLAYER_PLAY,MSIP_SOUND_RINGIN2,0);
		}
	}
}

static void on_nat_detect(const pj_stun_nat_detect_result *res)
{
	if (res->status != PJ_SUCCESS) {
		pjsua_perror(THIS_FILE, "NAT detection failed", res->status);
	} else {
		if (res->nat_type == PJ_STUN_NAT_TYPE_SYMMETRIC) {
			if (IsWindow(mainDlg->m_hWnd)) {
				CString message;
				pjsua_acc_config acc_cfg;
				pj_pool_t *pool;
				pool = pjsua_pool_create("acc_cfg-pjsua", 1000, 1000);
				if (pool) {
					if (pjsua_acc_get_config(account, pool, &acc_cfg)==PJ_SUCCESS) {
						acc_cfg.sip_stun_use = PJSUA_STUN_USE_DISABLED;
						acc_cfg.media_stun_use = PJSUA_STUN_USE_DISABLED;
						if (pjsua_acc_modify (account, &acc_cfg)==PJ_SUCCESS) {
							message = _T("STUN was automatically disabled.");
							message.Append(_T(" For more info visit MicroSIP website, help page."));

						}
					}
					pj_pool_release(pool);
				}
				mainDlg->BaloonPopup(_T("Symmetric NAT detected!"), message);
			}
		}
		PJ_LOG(3, (THIS_FILE, "NAT detected as %s", res->nat_type_name));
	}
}

static void on_buddy_state(pjsua_buddy_id buddy_id)
{
	if (IsWindow(mainDlg->m_hWnd)) {
		pjsua_buddy_info buddy_info;
		if (pjsua_buddy_get_info (buddy_id, &buddy_info) == PJ_SUCCESS) {
			Contact *contact = (Contact *) pjsua_buddy_get_user_data(buddy_id);
			int image;
			bool ringing = false;
			switch (buddy_info.status)
			{
			case PJSUA_BUDDY_STATUS_OFFLINE:
				image=1;
				break;
			case PJSUA_BUDDY_STATUS_ONLINE:
				if (PjToStr(&buddy_info.status_text)==_T("Ringing")) {
					ringing = true;
				}
				if (PjToStr(&buddy_info.status_text)==_T("On the phone")) {
					image=4;
				} else if (buddy_info.rpid.activity == PJRPID_ACTIVITY_AWAY)
				{
					image=2;
				} else if (buddy_info.rpid.activity == PJRPID_ACTIVITY_BUSY)
				{
					image=6;

				} else 
				{
					image=3;
				}
				break;
			default:
				image=0;
				break;
			}
			contact->image = image;
			contact->ringing = ringing;
			mainDlg->PostMessage(UM_ON_BUDDY_STATE,(WPARAM)contact);
		}
	}
}

static void on_pager2(pjsua_call_id call_id, const pj_str_t *from, const pj_str_t *to, const pj_str_t *contact, const pj_str_t *mime_type, const pj_str_t *body, pjsip_rx_data *rdata, pjsua_acc_id acc_id)
{
	if (IsWindow(mainDlg->m_hWnd)) {
		CString *number = new CString();
		CString *message = new CString();
		number->SetString(PjToStr(from, TRUE));
		message->SetString(PjToStr(body, TRUE));
		message->Trim();
		//-- fix wrong domain
		SIPURI sipuri;
		ParseSIPURI(*number, &sipuri);
		if (accountSettings.accountId && account == acc_id) {
			if (IsIP(sipuri.domain)) {
				sipuri.domain = accountSettings.account.domain;
			}
			if (!sipuri.user.IsEmpty()) {
				number->Format(_T("%s@%s"),sipuri.user,sipuri.domain);
			} else {
				number->SetString(sipuri.domain);
			}
		}
		//--
		mainDlg->PostMessage(UM_ON_PAGER, (WPARAM)number, (LPARAM)message);
	}
}

static void on_pager_status2(pjsua_call_id call_id, const pj_str_t *to, const pj_str_t *body, void *user_data, pjsip_status_code status, const pj_str_t *reason, pjsip_tx_data *tdata, pjsip_rx_data *rdata, pjsua_acc_id acc_id)
{
	if (status != 200) {
		if (IsWindow(mainDlg->m_hWnd)) {
			CString *number = new CString();
			CString *message = new CString();
			number->SetString(PjToStr(to, TRUE));
			message->SetString(PjToStr(reason, TRUE));
			message->Trim();
			//-- fix wrong domain
			SIPURI sipuri;
			ParseSIPURI(*number, &sipuri);
			if (accountSettings.accountId && account == acc_id) {
				if (IsIP(sipuri.domain)) {
					sipuri.domain = accountSettings.account.domain;
				}
				if (!sipuri.user.IsEmpty()) {
					number->Format(_T("%s@%s"),sipuri.user,sipuri.domain);
				} else {
					number->SetString(sipuri.domain);
				}
			}
			//--
			mainDlg->PostMessage(UM_ON_PAGER_STATUS, (WPARAM)number, (LPARAM)message);
		}
	}
}

static void on_call_transfer_status(pjsua_call_id call_id,
									int status_code,
									const pj_str_t *status_text,
									pj_bool_t final,
									pj_bool_t *p_cont)
{
	pjsua_call_info *call_info = new pjsua_call_info();
	if (pjsua_call_get_info(call_id, call_info) != PJ_SUCCESS || call_info->state == PJSIP_INV_STATE_NULL) {
		return;
	}

	CString *str = new CString();
	str->Format(_T("%s: %s"),
		Translate(_T("Call Transfer")),
		PjToStr(status_text, TRUE)
		);
	if (final) {
		str->AppendFormat(_T(" [%s]"), Translate(_T("Final")));
	}

	if (status_code/100 == 2) {
		*p_cont = PJ_FALSE;
	}

	call_info->last_status = (pjsip_status_code)status_code;

	PostMessage(mainDlg->m_hWnd, UM_ON_CALL_TRANSFER_STATUS, (WPARAM) call_info, (LPARAM) str);
}

LRESULT CmainDlg::onCallTransferStatus(WPARAM wParam,LPARAM lParam)
{
	pjsua_call_info *call_info = (pjsua_call_info *) wParam;
	CString *str = (CString *) lParam;

	MessagesContact* messagesContact = NULL;
	CString number = PjToStr(&call_info->remote_info, TRUE);
	messagesContact = mainDlg->messagesDlg->AddTab(number, _T(""), FALSE, call_info, TRUE, TRUE);
	if (messagesContact) {
		mainDlg->messagesDlg->AddMessage(messagesContact, *str);
	}
	if (call_info->last_status/100 == 2) {
		if (messagesContact) {
			messagesDlg->AddMessage(messagesContact, Translate(_T("Call transfered successfully, disconnecting call")));
		}
		msip_call_hangup_fast(call_info->id);
	}
	delete call_info;
	delete str;
	return 0;
}

static void on_call_transfer_request2(pjsua_call_id call_id, const pj_str_t *dst, pjsip_status_code *code, pjsua_call_setting *opt)
{
	SIPURI sipuri;
	ParseSIPURI(PjToStr(dst,TRUE),&sipuri);
	// display transfer request
	pj_bool_t cont;
	CString number = sipuri.user;
	if (number.IsEmpty()) {
		number = sipuri.domain;
	} else if (!accountSettings.accountId || sipuri.domain != accountSettings.account.domain){
		number.Append(_T("@")+sipuri.domain);
	}
	pj_str_t status_text = StrToPjStr(number);
	on_call_transfer_status(call_id,
		0,
		&status_text,
		PJ_FALSE,
		&cont);
	//--
	if (!code) {
		// if our function call
		return;
	}
	// deny transfer if we already have a call with same dest address
	pjsua_call_id call_ids[PJSUA_MAX_CALLS];
	unsigned calls_count = PJSUA_MAX_CALLS;
	unsigned calls_count_cmp = 0;
	if (pjsua_enum_calls ( call_ids, &calls_count)==PJ_SUCCESS)  {
		for (unsigned i = 0; i < calls_count; ++i) {
			pjsua_call_info call_info_curr;
			if (pjsua_call_get_info(call_ids[i], &call_info_curr) == PJ_SUCCESS) {
				SIPURI sipuri_curr;
				ParseSIPURI(PjToStr(&call_info_curr.remote_info, TRUE), &sipuri_curr);
				if (sipuri.user+_T("@")+sipuri.domain == sipuri_curr.user+_T("@")+sipuri_curr.domain
					) {
						*code = PJSIP_SC_DECLINE;
						break;
				}
			}
		}
	}
}

static void on_call_replace_request2(pjsua_call_id call_id, pjsip_rx_data *rdata, int *st_code, pj_str_t *st_text, pjsua_call_setting *opt)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
		if (!call_info.rem_vid_cnt) {
			opt->vid_cnt = 0;
		}
	} else {
		opt->vid_cnt = 0;
	}
}

static void on_call_replaced(pjsua_call_id old_call_id, pjsua_call_id new_call_id)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(new_call_id, &call_info) == PJ_SUCCESS) {
		on_call_transfer_request2(old_call_id,&call_info.remote_info,NULL,NULL);
	}
}

static void on_mwi_info(pjsua_acc_id acc_id, pjsua_mwi_info *mwi_info)
{
	if (mwi_info->rdata->msg_info.ctype) {
		const pjsip_ctype_hdr *ctype = mwi_info->rdata->msg_info.ctype;
		if (pj_strcmp2(&ctype->media.type,"application") != 0 || pj_strcmp2(&ctype->media.subtype,"simple-message-summary") != 0) {
			return;
		}
	}
	if (!mwi_info->rdata->msg_info.msg->body || !mwi_info->rdata->msg_info.msg->body->len) {
		return;
	}
	pjsip_msg_body *body = mwi_info->rdata->msg_info.msg->body;
	pj_scanner scanner;
	pj_scan_init(&scanner, (char*)body->data, body->len, PJ_SCAN_AUTOSKIP_WS, 0);
	bool hasMail = false;

	CString *number = new CString();
	char digit;
	int duration = 250;
	while (!pj_scan_is_eof(&scanner)) {
		pj_str_t key;
		pj_scan_get_until_chr(&scanner, ":", &key);
		pj_strtrim(&key);
		if (key.slen && !pj_scan_is_eof(&scanner)) {
			scanner.curptr++;
			pj_str_t value;
			pj_scan_get_until_chr(&scanner, "\r\n", &value);
			pj_strtrim(&value);
			if (pj_stricmp2(&key, "Messages-Waiting")==0) {
				hasMail = pj_stricmp2(&value,"yes")==0;
			} else if (pj_stricmp2(&key, "Message-Account")==0) {
				*number = PjToStr(&value,true);
			}
		}
	}
	pj_scan_fini(&scanner);
	if (!hasMail) {
		number->Empty();
	}
	PostMessage(mainDlg->m_hWnd, UM_ON_MWI_INFO, (WPARAM) number, 0);
}

LRESULT CmainDlg::onMWIInfo(WPARAM wParam,LPARAM lParam)
{
	if (wParam) {
		CString *number = (CString *) wParam;
		voicemailNumber = *number;
		delete number;
	} else {
		voicemailNumber.Empty();
	}
	voicemailButton.ShowWindow(!voicemailNumber.IsEmpty() ? SW_SHOW : SW_HIDE);
	return 0;
}

static void on_dtmf_digit(pjsua_call_id call_id, int digit)
{
	char signal[2];
	signal[0] = digit;
	signal[1] = 0;
	call_play_digit(-1, signal);
}

static void on_call_tsx_state(pjsua_call_id call_id, pjsip_transaction *tsx, pjsip_event *e)
{
	const pjsip_method info_method = {
		PJSIP_OTHER_METHOD,
		{ "INFO", 4 }
	};
	if (pjsip_method_cmp(&tsx->method, &info_method)==0) {
		/*
		* Handle INFO method.
		*/
		if (tsx->role == PJSIP_ROLE_UAS && tsx->state == PJSIP_TSX_STATE_TRYING) {
			if (e->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
				pjsip_rx_data *rdata = e->body.tsx_state.src.rdata;
				pjsip_msg_body *body = rdata->msg_info.msg->body;
				int code = 0;
				if (body && body->len
					&& pj_strcmp2(&body->content_type.type,"application")==0
					&& pj_strcmp2(&body->content_type.subtype,"dtmf-relay")==0) {
						code = 400;
						pj_scanner scanner;
						pj_scan_init(&scanner, (char*)body->data, body->len, PJ_SCAN_AUTOSKIP_WS, 0);
						char digit;
						int duration = 250;
						while (!pj_scan_is_eof(&scanner)) {
							pj_str_t key;
							pj_scan_get_until_chr(&scanner, "=", &key);
							pj_strtrim(&key);
							if (key.slen && !pj_scan_is_eof(&scanner)) {
								scanner.curptr++;
								pj_str_t value;
								pj_scan_get_until_chr(&scanner, "\r\n", &value);
								pj_strtrim(&value);
								if (pj_stricmp2(&key, "Signal")==0) {
									if (value.slen==1) {
										digit = *value.ptr;
										code = 200;
									}
								} else if (pj_stricmp2(&key, "Duration")==0) {
									int res = 0;
									for (int i=0; i<(unsigned)value.slen; ++i) {
										res = res * 10 + (value.ptr[i] - '0');
										res = res;
									}
									if (res>=100 || res<=5000) {
										duration = res;
									}
								}
							}
						}
						pj_scan_fini(&scanner);
						if (code == 200) {
							on_dtmf_digit(-1, digit);
						}
				} else if (!body || !body->len) {
					/* 200/OK */
					code = 200;
				}
				if (code) {
					/* Answer incoming INFO */
					pjsip_tx_data *tdata;
					if (pjsip_endpt_create_response(tsx->endpt, rdata,
						code, NULL, &tdata) == PJ_SUCCESS
						) {
							pjsip_tsx_send_msg(tsx, tdata);
					}
				}
			}
		}
	} else {
		if (tsx->state == PJSIP_TSX_STATE_COMPLETED) {
			// display declined REFER status
			const pjsip_method refer_method = {
				PJSIP_OTHER_METHOD,
				{ "REFER", 5 }
			};
			if (pjsip_method_cmp(&tsx->method, &refer_method)==0 && tsx->status_code/100 != 2) {
				pj_bool_t cont;
				on_call_transfer_status(call_id,
					tsx->status_code,
					&tsx->status_text,
					PJ_FALSE,
					&cont);
			}
		}
	}
}

CmainDlg::~CmainDlg(void)
{
}

void CmainDlg::OnDestroy()
{
	accountSettings.SettingsSave();

	if (mmNotificationClient) {
		delete mmNotificationClient;
	}
	WTSUnRegisterSessionNotification(m_hWnd);

	KillTimer(IDT_TIMER_IDLE);
	KillTimer(IDT_TIMER_CALL);

	PJDestroy();

	RemoveJumpList();
	if (tnd.hWnd) {
		Shell_NotifyIcon(NIM_DELETE, &tnd);
	}
	UnloadLangPackModule();

	CBaseDialog::OnDestroy();
}

void CmainDlg::PostNcDestroy()
{
	CBaseDialog::PostNcDestroy();
	delete this;
}

void CmainDlg::DoDataExchange(CDataExchange* pDX)
{
	CBaseDialog::DoDataExchange(pDX);
//	DDX_Control(pDX, IDD_MAIN, *mainDlg);
	DDX_Control(pDX, IDC_VOICEMAIL, voicemailButton);
}

BEGIN_MESSAGE_MAP(CmainDlg, CBaseDialog)
	ON_WM_CREATE()
	ON_WM_SYSCOMMAND()
	ON_WM_QUERYENDSESSION()
	ON_WM_TIMER()
	ON_WM_MOVE()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_CONTEXTMENU()
	ON_WM_DEVICECHANGE()
	ON_WM_SETCURSOR()
	ON_WM_WTSSESSION_CHANGE()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_VOICEMAIL, OnBnClickedVoicemail)
	ON_MESSAGE(UM_NOTIFYICON,onTrayNotify)
	ON_MESSAGE(UM_CREATE_RINGING,onCreateRingingDlg)
	ON_MESSAGE(UM_REFRESH_LEVELS,onRefreshLevels)
	ON_MESSAGE(UM_ON_REG_STATE2,onRegState2)
	ON_MESSAGE(UM_ON_CALL_STATE,onCallState)
	ON_MESSAGE(UM_ON_MWI_INFO,onMWIInfo)
	ON_MESSAGE(UM_ON_CALL_MEDIA_STATE,onCallMediaState)
	ON_MESSAGE(UM_ON_CALL_TRANSFER_STATUS,onCallTransferStatus)
	ON_MESSAGE(UM_ON_PLAYER_PLAY,onPlayerPlay)
	ON_MESSAGE(UM_ON_PLAYER_STOP,onPlayerStop)
	ON_MESSAGE(UM_ON_PAGER,onPager)
	ON_MESSAGE(UM_ON_PAGER_STATUS,onPagerStatus)
	ON_MESSAGE(UM_ON_BUDDY_STATE,onBuddyState)
	//ON_MESSAGE(UM_USERS_DIRECTORY,onUsersDirectoryLoaded)
	ON_MESSAGE(UM_SHELL_EXECUTE,onShellExecute)
	ON_MESSAGE(WM_POWERBROADCAST,onPowerBroadcast)
	ON_MESSAGE(WM_COPYDATA,onCopyData)
	ON_MESSAGE(UM_CALL_ANSWER,onCallAnswer)
	ON_MESSAGE(UM_CALL_HANGUP,onCallHangup)
	ON_MESSAGE(UM_TAB_ICON_UPDATE,onTabIconUpdate)
	ON_MESSAGE(UM_SET_PANE_TEXT,onSetPaneText)
	ON_MESSAGE(UM_ON_ACCOUNT,OnAccount)
	ON_COMMAND(ID_ACCOUNT_ADD,OnMenuAccountAdd)
	ON_COMMAND_RANGE(ID_ACCOUNT_CHANGE_RANGE,ID_ACCOUNT_CHANGE_RANGE+99,OnMenuAccountChange)
	ON_COMMAND_RANGE(ID_ACCOUNT_EDIT_RANGE,ID_ACCOUNT_EDIT_RANGE+99,OnMenuAccountEdit)
	ON_COMMAND(ID_SETTINGS,OnMenuSettings)
	ON_COMMAND(ID_SHORTCUTS,OnMenuShortcuts)
	ON_COMMAND(ID_ALWAYS_ON_TOP,OnMenuAlwaysOnTop)
	ON_COMMAND(ID_LOG,OnMenuLog)
	ON_COMMAND(ID_EXIT,OnMenuExit)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB, &CmainDlg::OnTcnSelchangeTab)
	ON_NOTIFY(TCN_SELCHANGING, IDC_TAB, &CmainDlg::OnTcnSelchangingTab)
	ON_COMMAND(ID_WEBSITE,OnMenuWebsite)
	ON_COMMAND(ID_DONATE,OnMenuDonate)
#ifdef _GLOBAL_VIDEO
#endif
END_MESSAGE_MAP()


// CmainDlg message handlers

void CmainDlg::OnBnClickedOk() 
{
}

void CmainDlg::OnBnClickedVoicemail()
{
	if (accountSettings.accountId && !accountSettings.account.voicemailNumber.IsEmpty()) {
		MakeCall(accountSettings.account.voicemailNumber);
	} else {
		MakeCall(voicemailNumber);
	}
}

CmainDlg::CmainDlg(CWnd* pParent /*=NULL*/)
: CBaseDialog(CmainDlg::IDD, pParent)
{
#ifdef _DEBUG
	if (AllocConsole()) {
		HANDLE console = NULL;
		console = GetStdHandle( STD_OUTPUT_HANDLE );
		freopen( "CONOUT$", "wt", stdout );
	}
#endif

	this->m_hWnd = NULL;
	mmNotificationClient = NULL;

	mainDlg = this;
	widthAdd = 0;
	heightAdd = 0;

	m_tabPrev = -1;

	CString audioCodecsCaptions = _T("opus/16000/1;Opus 16 kHz;\
opus/24000/1;Opus 24 kHz;\
opus/48000/1;Opus 48 kHz;\
PCMA/8000/1;G.711 A-law;\
PCMU/8000/1;G.711 u-law;\
G722/16000/1;G.722 16 kHz;\
G729/8000/1;G.729 8 kHz;\
GSM/8000/1;GSM 8 kHz;\
AMR/8000/1;AMR 8 kHz;\
iLBC/8000/1;iLBC 8 kHz;\
speex/32000/1;Speex 32 kHz;\
speex/16000/1;Speex 16 kHz;\
speex/8000/1;Speex 8 kHz;\
SILK/24000/1;SILK 24 kHz;\
SILK/16000/1;SILK 16 kHz;\
SILK/12000/1;SILK 12 kHz;\
SILK/8000/1;SILK 8 kHz;\
L16/44100/1;LPCM 44 kHz;\
L16/44100/2;LPCM 44 kHz Stereo;\
L16/16000/1;LPCM 16 kHz;\
L16/16000/2;LPCM 16 kHz Stereo;\
L16/8000/1;LPCM 8 kHz;\
L16/8000/2;LPCM 8 kHz Stereo");
	int pos = 0;
	CString resToken = audioCodecsCaptions.Tokenize(_T(";"),pos);
	while (!resToken.IsEmpty()) {
		audioCodecList.AddTail(resToken);
		resToken = audioCodecsCaptions.Tokenize(_T(";"),pos);
	}

	wchar_t szBuf[STR_SZ];
	wchar_t szLocale[STR_SZ];
	::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SENGLANGUAGE, szBuf, STR_SZ);
	_tcscpy(szLocale, szBuf);
	::GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SENGCOUNTRY, szBuf, STR_SZ);
	if (_tcsclen(szBuf) != 0){
		_tcscat(szLocale, _T("_"));
		_tcscat(szLocale, szBuf);
	}
	::GetLocaleInfo(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, szBuf, STR_SZ);
	if (_tcsclen(szBuf) != 0){
		_tcscat(szLocale, _T("."));
		_tcscat(szLocale, szBuf);
	}
	_tsetlocale(LC_ALL, szLocale); // e.g. szLocale = "English_United States.1252"

	LoadLangPackModule();

	Create (IDD, pParent);
}

int CmainDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{

	if (accountSettings.noResize) {
		ModifyStyle(WS_MAXIMIZEBOX | WS_THICKFRAME, WS_POPUPWINDOW);
	}

	if (langPack.rtl) {
		ModifyStyleEx(0,WS_EX_LAYOUTRTL);
	}

	ShortcutsLoad();
	shortcutsEnabled = accountSettings.enableShortcuts;
	shortcutsBottom = accountSettings.shortcutsBottom;
	if (accountSettings.enableShortcuts) {
		if (shortcutsBottom) {
			heightAdd = 10 + shortcuts.GetCount() * 30;
		} else {
			widthAdd = 140;
		}
		SetWindowPos(NULL, 0, 0, lpCreateStruct->cx + widthAdd, lpCreateStruct->cy + heightAdd, SWP_NOMOVE | SWP_NOZORDER);
	}
	return CBaseDialog::OnCreate(lpCreateStruct);
}

BOOL CmainDlg::OnInitDialog()
{
	CBaseDialog::OnInitDialog();
	if ( lstrcmp(theApp.m_lpCmdLine,_T("/hidden")) == 0) {
		accountSettings.hidden = TRUE;
		theApp.m_lpCmdLine = NULL;
	}

	WTSRegisterSessionNotification(m_hWnd,NOTIFY_FOR_THIS_SESSION);
	mmNotificationClient = new CMMNotificationClient();
	
	pj_ready = false;

	callTimer = 0;

	autoAnswerCallId = PJSUA_INVALID_ID;

	settingsDlg = NULL;
	shortcutsDlg = NULL;

	messagesDlg = new MessagesDlg(this);
	transferDlg = NULL;
	accountDlg = NULL;

	m_lastInputTime = 0;
	m_idleCounter = 0;
	m_isAway = FALSE;

#ifdef _GLOBAL_VIDEO
	previewWin = NULL;
#endif

	SetTimer(IDT_TIMER_IDLE,5000,NULL);

	if (!accountSettings.hidden) {

		SetupJumpList();

		m_hIcon = theApp.LoadIcon(IDR_MAINFRAME);
		iconSmall = (HICON)LoadImage(
			AfxGetInstanceHandle(),
			MAKEINTRESOURCE(IDR_MAINFRAME),
			IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED );
		PostMessage(WM_SETICON,ICON_SMALL,(LPARAM)iconSmall);

		TranslateDialog(this->m_hWnd);

		// Set the icon for this dialog.  The framework does this automatically
		//  when the application's main window is not a dialog
		SetIcon(m_hIcon, TRUE);			// Set big icon
		SetIcon(m_hIcon, FALSE);		// Set small icon

		// TODO: Add extra initialization here

		// add tray icon
		CString str;
		str.Format(_T("%s %s"), _T(_GLOBAL_NAME_NICE), _T(_GLOBAL_VERSION));
		tnd.cbSize = NOTIFYICONDATA_V3_SIZE;
		tnd.hWnd = this->GetSafeHwnd();
		tnd.uID = IDR_MAINFRAME;
		tnd.uCallbackMessage = UM_NOTIFYICON;
		tnd.uFlags = NIF_MESSAGE|NIF_ICON|NIF_TIP; 
		iconInactive = LoadIcon(AfxGetInstanceHandle(),MAKEINTRESOURCE(IDI_INACTIVE));
		tnd.hIcon = iconInactive;
		lstrcpyn(tnd.szTip, (LPCTSTR)str, sizeof(tnd.szTip));
		DWORD dwMessage = NIM_ADD;
		Shell_NotifyIcon(dwMessage, &tnd);
	} else {
		tnd.hWnd = NULL;
	}

	m_bar.Create(this);
	m_bar.SetIndicators(indicators,1);
	m_bar.SetPaneInfo(0,IDS_STATUSBAR, SBPS_STRETCH , 0);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, IDS_STATUSBAR);

	m_bar.SetPaneText( 0, _T(""));

	AutoMove(m_bar.m_hWnd,0,100,100,0);

	voicemailButton.LoadBitmaps(IDB_VOICEMAIL,IDB_VOICEMAIL2);
	voicemailButton.SizeToContent();

	m_hCursorHand = ::LoadCursor(NULL, IDC_HAND);
	
	//--set window pos
	CRect screenRect;
	GetScreenRect(&screenRect);
	CRect rect;
	GetWindowRect(&rect);

	windowSize.x = rect.Width();
	windowSize.y = rect.Height();

	int mx;
	int my;
	int mW = accountSettings.mainW>0?accountSettings.mainW:_GLOBAL_WIDTH_DEFAULT;
	int mH = accountSettings.mainH>0?accountSettings.mainH:(rect.Height()+30);
	// coors not specified, first run
	if (!accountSettings.mainX && !accountSettings.mainY) {
		CRect primaryScreenRect;
		SystemParametersInfo(SPI_GETWORKAREA,0,&primaryScreenRect,0);
		mx = primaryScreenRect.Width()-mW-widthAdd;
		my = primaryScreenRect.Height()-mH;
	} else {
		int maxLeft = screenRect.right-mW;
		if (accountSettings.mainX>maxLeft) {
			mx = maxLeft;
		} else {
			mx = accountSettings.mainX < screenRect.left ? screenRect.left : accountSettings.mainX;
		}
		int maxTop = screenRect.bottom-mH;
		if (accountSettings.mainY>maxTop) {
			my = maxTop;
		} else {
			my = accountSettings.mainY < screenRect.top ? screenRect.top : accountSettings.mainY;
		}
	}

	//--set messages window pos/size
	messagesDlg->GetWindowRect(&rect);
	int messagesX;
	int messagesY;
	int messagesW = accountSettings.messagesW>0?accountSettings.messagesW:550;
	int messagesH = accountSettings.messagesH>0?accountSettings.messagesH:mH;
	// coors not specified, first run
	if (!accountSettings.messagesX && !accountSettings.messagesY) {
		accountSettings.messagesX = mx - messagesW;
		accountSettings.messagesY = my;
	}
	int maxLeft = screenRect.right-messagesW;
	if (accountSettings.messagesX>maxLeft) {
		messagesX = maxLeft;
	} else {
		messagesX = accountSettings.messagesX < screenRect.left ? screenRect.left : accountSettings.messagesX;
	}
	int maxTop = screenRect.bottom-messagesH;
	if (accountSettings.messagesY>maxTop) {
		messagesY = maxTop;
	} else {
		messagesY = accountSettings.messagesY < screenRect.top ? screenRect.top : accountSettings.messagesY;
	}

	messagesDlg->SetWindowPos(NULL, messagesX, messagesY, messagesW, messagesH, SWP_NOZORDER);

	SetWindowPos(accountSettings.alwaysOnTop?&CWnd::wndTopMost:&CWnd::wndNoTopMost, mx, my, mW, mH, NULL);

	CRect pageRect;

	CTabCtrl* tab = (CTabCtrl*) GetDlgItem(IDC_TAB);

	if (widthAdd || heightAdd) {
		tab->GetWindowRect(pageRect);
		tab->SetWindowPos(NULL, 0, 0, pageRect.Width()+widthAdd, pageRect.Height()+heightAdd, SWP_NOZORDER | SWP_NOMOVE);
		if (widthAdd) {
			CWnd* wnd;
			wnd = GetDlgItem(IDC_VOICEMAIL);
			wnd->GetWindowRect(pageRect);
			ScreenToClient(pageRect);
			wnd->SetWindowPos(NULL, pageRect.left+widthAdd, pageRect.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
		}
	}

	AutoMove(IDC_TAB,0,0,100,0);
	AutoMove(IDC_VOICEMAIL,100,0,0,0);

	tab->SetMinTabWidth(1);
	TC_ITEM tabItem;
	tabItem.mask = TCIF_TEXT | TCIF_PARAM;

	pageDialer = new Dialer(this);
	tabItem.pszText = Translate(_T("Dialpad"));
	tabItem.lParam = (LPARAM)pageDialer;
	tab->InsertItem( 99, &tabItem );
	pageDialer->SetWindowPos(NULL, 0, 32, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	AutoMove(pageDialer->m_hWnd,0,0,100,100);

		pageCalls = new Calls(this);
		pageCalls->OnCreated();
		tabItem.pszText = Translate(_T("Calls"));
		tabItem.lParam = (LPARAM)pageCalls;
		tab->InsertItem( 99, &tabItem );
		pageCalls->GetWindowRect(pageRect);
		pageCalls->SetWindowPos(NULL, 0, 32, pageRect.Width()+widthAdd, pageRect.Height()+heightAdd, SWP_NOZORDER);
		AutoMove(pageCalls->m_hWnd,0,0,100,100);

		pageContacts = new Contacts(this);
		pageContacts->OnCreated();
		tabItem.pszText = Translate(_T("Contacts"));
		tabItem.lParam = (LPARAM)pageContacts;
		tab->InsertItem( 99, &tabItem );
		pageContacts->GetWindowRect(pageRect);
		pageContacts->SetWindowPos(NULL, 0, 32, pageRect.Width()+widthAdd, pageRect.Height()+heightAdd, SWP_NOZORDER);
		AutoMove(pageContacts->m_hWnd,0,0,100,100);

	tabItem.pszText = Translate(_T("Menu"));
	tabItem.lParam = -1;
	tab->InsertItem( 99, &tabItem );
	tabItem.pszText = _T("?");
	tabItem.lParam = -2;
	tab->InsertItem( 99, &tabItem );
	tab->SetCurSel(accountSettings.activeTab);


	BOOL minimized = !lstrcmp(theApp.m_lpCmdLine, _T("/minimized"));
	if (minimized) {
		theApp.m_lpCmdLine = _T("");
	}
	if (accountSettings.silent) {
		minimized = true;
	}

	if (!accountSettings.hidden) {
		ShowWindow(SW_SHOW); // need show before hide, to hide properly
	}

	m_startMinimized = !firstRun && minimized;

	disableAutoRegister = FALSE;
	SetWindowText(_T(_GLOBAL_NAME_NICE));
	UpdateWindowText();
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CmainDlg::OnCreated() {
	//-- PJSIP create
	while (!pj_ready) {
		PJCreate();
		if (pj_ready) {
			break;
		}
		if (AfxMessageBox(_T("Unable to initialize network sockets."),MB_RETRYCANCEL|MB_ICONEXCLAMATION)!=IDRETRY) {
			DestroyWindow();
			return;
		}
	}
	if (lstrlen(theApp.m_lpCmdLine)) {
		DialNumberFromCommandLine(theApp.m_lpCmdLine);
		theApp.m_lpCmdLine = NULL;
	}
	PJAccountAdd();
	//--
}

void CmainDlg::BaloonPopup(CString title, CString message, DWORD flags)
{
	if (tnd.hWnd) {
		lstrcpyn(tnd.szInfo, message, sizeof(tnd.szInfo));
		lstrcpyn(tnd.szInfoTitle, title, sizeof(tnd.szInfoTitle));
		tnd.uFlags = tnd.uFlags | NIF_INFO; 
		tnd.dwInfoFlags = flags;
		DWORD dwMessage = NIM_MODIFY;
		Shell_NotifyIcon(dwMessage, &tnd);
	}
}

void CmainDlg::OnMenuAccountAdd()
{
	if (!accountSettings.hidden) {
		if (!accountDlg) {
			accountDlg = new AccountDlg(this);
		} else {
			accountDlg->SetForegroundWindow();
		}
		accountDlg->Load(0);
	}
}
void CmainDlg::OnMenuAccountChange(UINT nID)
{
	if (accountSettings.accountId) {
		PJAccountDelete();
	}
	int idNew = nID - ID_ACCOUNT_CHANGE_RANGE + 1;
	if (accountSettings.accountId != idNew ) {
		accountSettings.accountId = idNew;
		accountSettings.AccountLoad(accountSettings.accountId,&accountSettings.account);
	} else {
		accountSettings.accountId = 0;
	}
	accountSettings.SettingsSave();
	mainDlg->PJAccountAdd();
}

void CmainDlg::OnMenuAccountEdit(UINT nID)
{
	if (!accountDlg) {
		accountDlg = new AccountDlg(this);
	} else {
		accountDlg->SetForegroundWindow();
	}
	int id = accountSettings.accountId>0 ? accountSettings.accountId : nID - ID_ACCOUNT_EDIT_RANGE + 1;
	accountDlg->Load(id);
}

void CmainDlg::OnMenuSettings()
{
	if (!accountSettings.hidden) {
		if (!settingsDlg)
		{
			settingsDlg = new SettingsDlg(this);
		} else {
			settingsDlg->SetForegroundWindow();
		}
	}
}

void CmainDlg::OnMenuShortcuts()
{
	if (!shortcutsDlg)
	{
		shortcutsDlg = new ShortcutsDlg(this);
	} else {
		shortcutsDlg->SetForegroundWindow();
	}
}

void CmainDlg::OnMenuAlwaysOnTop()
{
	accountSettings.alwaysOnTop = 1 - accountSettings.alwaysOnTop;
	AccountSettingsPendingSave();
	SetWindowPos(accountSettings.alwaysOnTop?&this->wndTopMost:&this->wndNoTopMost,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
}

void CmainDlg::OnMenuLog()
{
	ShellExecute(NULL, NULL, accountSettings.logFile, NULL, NULL, SW_SHOWNORMAL);
}

void CmainDlg::OnMenuExit()
{
	this->DestroyWindow ();
}

LRESULT CmainDlg::onTrayNotify(WPARAM wParam,LPARAM lParam)
{
	UINT uMsg = (UINT) lParam; 
	switch (uMsg ) 
	{ 
	case WM_LBUTTONUP:
		if (this->IsWindowVisible() && !IsIconic())
		{
			if (wParam) {
				this->ShowWindow(SW_HIDE);
			} else {
				SetForegroundWindow();
			}
		} else
		{
			bool blockRestore = false;
			if (accountSettings.hidden) {
				blockRestore = true;
			}
			if (!blockRestore) {
				if (IsIconic()) {
					ShowWindow(SW_RESTORE);
				} else {
					ShowWindow(SW_SHOW);
				}
				SetForegroundWindow();
				CTabCtrl* tab = (CTabCtrl*) GetDlgItem(IDC_TAB);
				int nTab = tab->GetCurSel();
				TC_ITEM tci;
				tci.mask = TCIF_PARAM;
				tab->GetItem(nTab, &tci);
				CWnd* pWnd = (CWnd *)tci.lParam;
				pWnd->SetFocus();
			}
		}
		break;
	case WM_RBUTTONUP:
		MainPopupMenu();
		break;
	} 
	return TRUE;
}

void CmainDlg::MainPopupMenu() 
{
	CString str;
	CPoint point;    
	GetCursorPos(&point);
	CMenu menu;
	menu.LoadMenu(IDR_MENU_TRAY);
	CMenu* tracker = menu.GetSubMenu(0);
	TranslateMenu(tracker->m_hMenu);

	if (!accountSettings.enableLog) {
		tracker->EnableMenuItem(ID_LOG, MF_DISABLED | MF_GRAYED);
	}
	if (accountSettings.alwaysOnTop) {
		tracker->CheckMenuItem(ID_ALWAYS_ON_TOP,MF_CHECKED);
	}

	tracker->InsertMenu(0, MF_BYPOSITION, ID_ACCOUNT_ADD, Translate(_T("Add Account...")));

	CMenu editMenu;
	editMenu.CreatePopupMenu();
	Account account;
	int i = 0;
	bool checked = false;
	while (true) {
		if (!accountSettings.AccountLoad(i+1,&account)) {
			break;
		}

		if (!account.label.IsEmpty()) {
			str = account.label;
		} else {
			str.Format(_T("%s@%s"),account.username,account.domain);
		}
		tracker->InsertMenu(ID_ACCOUNT_ADD, (accountSettings.accountId == i+1 ? MF_CHECKED : 0), ID_ACCOUNT_CHANGE_RANGE+i, str);
		editMenu.AppendMenu(0, ID_ACCOUNT_EDIT_RANGE+i, str);
		if (!checked) {
			checked = accountSettings.accountId == i+1;
		}
		i++;
	}
	CString rab = Translate(_T("Edit Account"));
	rab.Append(_T("\tCtrl+M"));
	if (i==1) {
		MENUITEMINFO menuItemInfo;
		menuItemInfo.cbSize = sizeof(MENUITEMINFO);
		menuItemInfo.fMask=MIIM_STRING;
		menuItemInfo.dwTypeData = Translate(_T("Make Active"));
		tracker->SetMenuItemInfo(ID_ACCOUNT_CHANGE_RANGE,&menuItemInfo);
		tracker->InsertMenu(ID_ACCOUNT_ADD, 0, ID_ACCOUNT_EDIT_RANGE, rab);
	} else if (i>1) {
		tracker->InsertMenu(ID_ACCOUNT_ADD, MF_SEPARATOR);
		if (checked) {
			tracker->InsertMenu(ID_ACCOUNT_ADD, 0, ID_ACCOUNT_EDIT_RANGE, rab);
		} else {
			tracker->InsertMenu(ID_ACCOUNT_ADD, MF_POPUP, (UINT)editMenu.m_hMenu, Translate(_T("Edit Account")));
		}
	}

	SetForegroundWindow();
	tracker->TrackPopupMenu( 0, point.x, point.y, this );
	PostMessage(WM_NULL, 0, 0);
}

LRESULT CmainDlg::onCreateRingingDlg(WPARAM wParam,LPARAM lParam)
{
	pjsua_call_info* call_info = (pjsua_call_info*) wParam;
	
	if (!accountSettings.silent) {

	call_user_data *user_data = (call_user_data*)lParam;

	RinginDlg* ringinDlg = new RinginDlg(this);

	if (call_info->rem_vid_cnt) {
		((CButton*)ringinDlg->GetDlgItem(IDC_VIDEO))->EnableWindow(TRUE);
	}
	ringinDlg->GotoDlgCtrl(ringinDlg->GetDlgItem(IDC_CALLER_NAME));

	ringinDlg->call_id = call_info->id;
	SIPURI sipuri;
	CStringW rab;
	CString str;
	CString info;

	info = PjToStr(&call_info->remote_info, TRUE);

	ParseSIPURI(info, &sipuri);
	CString name = pageContacts->GetNameByNumber(GetSIPURI(sipuri.user, true));
	if (name.IsEmpty()) {
		name = !sipuri.name.IsEmpty() ? sipuri.name : (!sipuri.user.IsEmpty()?sipuri.user:sipuri.domain);
	}
	if (!user_data->diversion.IsEmpty()) {
		name.Format(_T("%s -> %s"),user_data->diversion,name);
	}
	ringinDlg->GetDlgItem(IDC_CALLER_NAME)->SetWindowText(name);

	int c = 0;
	int len=0;

	info=(!sipuri.user.IsEmpty()?sipuri.user+_T("@"):_T(""))+sipuri.domain;
	if (!sipuri.name.IsEmpty() && sipuri.name!=name) {
		info = sipuri.name + _T(" <") + info + _T(">");
	}
	len+=info.GetLength();
	str.Format(_T("%s\r\n\r\n"), info);

	info = PjToStr(&call_info->local_info, TRUE);
	ParseSIPURI(info, &sipuri);
	info=(!sipuri.user.IsEmpty()?sipuri.user+_T("@"):_T(""))+sipuri.domain;
	len+=info.GetLength();
	str.AppendFormat(_T("%s: %s\r\n\r\n"), Translate(_T("To")), info);

	if (len<70) {
		c++;
	}

	if (!user_data->userAgent.IsEmpty()) {
		str.AppendFormat(_T("%s: %s"), Translate(_T("User-Agent")), user_data->userAgent);
	} else {
		c++;
	}

	for (int i=0; i<c; i++) {
		str = _T("\r\n") + str;
	}

	
	if (str != name) {
		ringinDlg->GetDlgItem(IDC_CALLER_ADDR)->SetWindowText(str);
	} else {
		ringinDlg->GetDlgItem(IDC_CALLER_ADDR)->EnableWindow(FALSE);
	}
	ringinDlgs.Add(ringinDlg);

	}
	return 0;
}

LRESULT CmainDlg::onRefreshLevels(WPARAM wParam,LPARAM lParam)
{
	pageDialer->OnVScroll(0, 0, NULL);
	return 0;
}

LRESULT CmainDlg::onPager(WPARAM wParam,LPARAM lParam)
{
	CString *number=(CString *)wParam;
	CString *message=(CString *)lParam;
	MessagesContact* messagesContact = messagesDlg->AddTab(*number);
	if (messagesContact) {
		messagesDlg->AddMessage(messagesContact, *message, MSIP_MESSAGE_TYPE_REMOTE);
		onPlayerPlay(MSIP_SOUND_MESSAGE_IN, 0);
	}
	delete number;
	delete message;
	return 0;
}

LRESULT CmainDlg::onPagerStatus(WPARAM wParam,LPARAM lParam)
{
	CString *number=(CString *)wParam;
	CString *message=(CString *)lParam;
	MessagesContact* messagesContact = mainDlg->messagesDlg->AddTab(*number);
	if (messagesContact) {
		mainDlg->messagesDlg->AddMessage(messagesContact, *message);
	}
	delete number;
	delete message;
	return 0;
}

LRESULT CmainDlg::onBuddyState(WPARAM wParam,LPARAM lParam)
{
	Contact *contact = (Contact *)wParam;
	CListCtrl *list= (CListCtrl*)pageContacts->GetDlgItem(IDC_CONTACTS);
	int n = list->GetItemCount();
	for (int i=0; i<n; i++) {
		if (contact==(Contact *)list->GetItemData(i)) {
			list->SetItem(i, 0, LVIF_IMAGE, 0, contact->image, 0, 0, 0);
		}
	}
	if (contact->ringing) {
		SetTimer(IDT_TIMER_CONTACTS_BLINK,500,NULL);
		OnTimerContactBlink();
	}
	return 0;
}

LRESULT CmainDlg::onShellExecute(WPARAM wParam,LPARAM lParam)
{
	ShellExecute(NULL, NULL, (LPTSTR)wParam, (LPTSTR)lParam, NULL, SW_HIDE);
	return 0;
}

LRESULT CmainDlg::onPowerBroadcast(WPARAM wParam,LPARAM lParam)
{
	if (wParam == PBT_APMRESUMEAUTOMATIC) {
		PJAccountAdd();
	} else if (wParam == PBT_APMSUSPEND) {
		PJAccountDelete();
	}
	return TRUE;
}

LRESULT CmainDlg::OnAccount(WPARAM wParam,LPARAM lParam)
{
	if (!accountDlg) {
		accountDlg = new AccountDlg(this);
	} else {
		accountDlg->SetForegroundWindow();
	}
	accountDlg->Load(accountSettings.accountId);
	if (wParam && accountDlg) {
		CEdit *edit = (CEdit*)accountDlg->GetDlgItem(IDC_EDIT_PASSWORD);
		if (edit) {
			edit->SetFocus();
			int nLength = edit->GetWindowTextLength();
			edit->SetSel(nLength,nLength);
		}
	}
	return 0;
}

void CmainDlg::OnTimerCall ()
{
	pjsua_call_id call_id;
	int duration = messagesDlg->GetCallDuration(&call_id);
	if (duration!=-1) {
		CString str;
		str.Format(_T("%s %s"),Translate(_T("Connected")), GetDuration(duration, true));
		call_user_data *user_data = (call_user_data *) pjsua_call_get_user_data(call_id);
		unsigned icon = IDI_ACTIVE;
		if (user_data) {
			if (user_data->srtp == MSIP_SRTP) {
				icon = IDI_ACTIVE_SECURE;
			}
		}
		UpdateWindowText(str, icon);
	} else {
		if (KillTimer(IDT_TIMER_CALL)) {
			callTimer = 0;
		}
	}
}

void CmainDlg::OnTimerContactBlink ()
{
	CListCtrl *list= (CListCtrl*)pageContacts->GetDlgItem(IDC_CONTACTS);
	int n = list->GetItemCount();
	bool ringing = false;
	for (int i=0; i<n; i++) {
		Contact *contact = (Contact *)list->GetItemData(i);
		if (contact->ringing) {
			list->SetItem(i, 0, LVIF_IMAGE, 0, timerContactBlinkState?5:contact->image, 0, 0, 0);
			ringing = true;
		} else {
			list->SetItem(i, 0, LVIF_IMAGE, 0, contact->image, 0, 0, 0);
		}
	}
	if (!ringing) {
		KillTimer(IDT_TIMER_CONTACTS_BLINK);
		timerContactBlinkState=false;
	} else {
		timerContactBlinkState = !timerContactBlinkState;
	}
}

void CmainDlg::OnTimer (UINT TimerVal)
{
	if (TimerVal == IDT_TIMER_AUTOANSWER) {
		KillTimer(IDT_TIMER_AUTOANSWER);
		AutoAnswer(autoAnswerCallId);
		autoAnswerCallId = PJSUA_INVALID_ID;
	} else if (TimerVal == IDT_TIMER_SWITCH_DEVICES) {
		KillTimer(IDT_TIMER_SWITCH_DEVICES);
		if (pj_ready) {
			PJ_LOG(3, (THIS_FILE, "Execute refresh devices"));
			bool snd_is_active = pjsua_snd_is_active();
			bool is_ring;
			if (snd_is_active) {
				int in,out;
				if (pjsua_get_snd_dev(&in,&out)==PJ_SUCCESS) {
					is_ring = (out == audio_ring);
				} else {
					is_ring = false;
				}
				pjsua_set_null_snd_dev();
			}
			pjmedia_aud_dev_refresh();
			UpdateSoundDevicesIds();
#ifdef _GLOBAL_VIDEO
			pjmedia_vid_dev_refresh();
#endif
			if (snd_is_active) {
				SetSoundDevice(is_ring?audio_ring:audio_output,true);
			}
		} else {
			PJ_LOG(3, (THIS_FILE, "Cancel refresh devices"));
		}
	} else if (TimerVal == IDT_TIMER_SAVE) {
		KillTimer(IDT_TIMER_SAVE);
		accountSettings.SettingsSave();
	} else if (TimerVal == IDT_TIMER_DIRECTORY) {
		UsersDirectoryLoad();
	} else if (TimerVal == IDT_TIMER_CONTACTS_BLINK) {
		OnTimerContactBlink();
	} else if (TimerVal == IDT_TIMER_CALL) {
		OnTimerCall();
	} else
			if (TimerVal == IDT_TIMER_IDLE) {
				if (pj_ready) {
					//--
					LASTINPUTINFO lii;
					lii.cbSize = sizeof(LASTINPUTINFO);
					if (GetLastInputInfo(&lii)) {
						if (lii.dwTime != m_lastInputTime) {
							m_lastInputTime = lii.dwTime;
							m_idleCounter = 0;
							if (m_isAway) {
								PublishStatus();
							}
						} else {
							m_idleCounter++;
							if (m_idleCounter == 120) {
								PublishStatus(false);
							}
						}
					}
					//--
				}
			} else
			if ( TimerVal = IDT_TIMER_TONE ) {
				onPlayerPlay(MSIP_SOUND_RINGOUT, 0);
			}
}

void CmainDlg::PJCreate()
{
	pageContacts->isSubscribed = FALSE;
	player_id = PJSUA_INVALID_ID;

	// check updates
	if (accountSettings.updatesInterval != _T("never")) 
	{
		CTime t = CTime::GetCurrentTime();
		time_t time = t.GetTime();
		int days;
		if (accountSettings.updatesInterval ==_T("daily"))
		{
			days = 1;
		} else if (accountSettings.updatesInterval ==_T("monthly"))
		{
			days = 30;
		} else if (accountSettings.updatesInterval ==_T("quarterly"))
		{
			days = 90;
		} else
		{
			days = 7;
		}
		if (accountSettings.checkUpdatesTime + days * 86400 < time)
		{
			CheckUpdates();
			accountSettings.checkUpdatesTime = time;
			accountSettings.SettingsSave();
		}
	}

	// pj create
	pj_status_t status;
	pjsua_config         ua_cfg;
	pjsua_media_config   media_cfg;
	pjsua_transport_config cfg;

	// Must create pjsua before anything else!
	status = pjsua_create();
	if (status != PJ_SUCCESS) {
		return;
	}

	ua_cfg.max_calls = 16;

	// Initialize configs with default settings.
	pjsua_config_default(&ua_cfg);
	pjsua_media_config_default(&media_cfg);

	CString userAgent;
	if (accountSettings.userAgent.IsEmpty()) {
		userAgent.Format(_T("%s/%s"), _T(_GLOBAL_NAME_NICE), _T(_GLOBAL_VERSION));
		ua_cfg.user_agent = StrToPjStr(userAgent);
	} else {
		ua_cfg.user_agent = StrToPjStr(accountSettings.userAgent);
	}

	ua_cfg.cb.on_reg_state2=&on_reg_state2;
	ua_cfg.cb.on_call_state=&on_call_state;
	ua_cfg.cb.on_dtmf_digit=&on_dtmf_digit;
	ua_cfg.cb.on_call_tsx_state=&on_call_tsx_state;
		
	ua_cfg.cb.on_call_media_state = &on_call_media_state;
	ua_cfg.cb.on_incoming_call = &on_incoming_call;
	ua_cfg.cb.on_nat_detect= &on_nat_detect;
	ua_cfg.cb.on_buddy_state = &on_buddy_state;
	ua_cfg.cb.on_pager2 = &on_pager2;
	ua_cfg.cb.on_pager_status2 = &on_pager_status2;
	ua_cfg.cb.on_call_transfer_request2 = &on_call_transfer_request2;	
	ua_cfg.cb.on_call_transfer_status = &on_call_transfer_status;

	ua_cfg.cb.on_call_replace_request2 = &on_call_replace_request2;
	ua_cfg.cb.on_call_replaced = &on_call_replaced;

	ua_cfg.cb.on_mwi_info = &on_mwi_info;

	ua_cfg.srtp_secure_signaling=0;

/*
TODO: accountSettings.account: public_addr
*/

	if (accountSettings.enableSTUN && !accountSettings.stun.IsEmpty()) 
	{
		ua_cfg.stun_srv_cnt=1;
		ua_cfg.stun_srv[0] = StrToPjStr( accountSettings.stun );
	}

	media_cfg.enable_ice = PJ_FALSE;
	media_cfg.no_vad = accountSettings.vad ? PJ_FALSE : PJ_TRUE;
	media_cfg.ec_tail_len = accountSettings.ec ? PJSUA_DEFAULT_EC_TAIL_LEN : 0;

	media_cfg.clock_rate=44100;
	media_cfg.channel_count=2;

	// Initialize pjsua
	if (accountSettings.enableLog) {
		pjsua_logging_config log_cfg;
		pjsua_logging_config_default(&log_cfg);
		//--
		CStringA buf;
		int len = accountSettings.logFile.GetLength()+1;
		LPSTR pBuf = buf.GetBuffer(len);
		WideCharToMultiByte(CP_ACP,0,accountSettings.logFile.GetBuffer(),-1,pBuf,len,NULL,NULL);
		log_cfg.log_filename = pj_str(pBuf);
		log_cfg.decor |= PJ_LOG_HAS_CR;
		accountSettings.logFile = pBuf;
		//--
		status = pjsua_init(&ua_cfg, &log_cfg, &media_cfg);
	} else {
		status = pjsua_init(&ua_cfg, NULL, &media_cfg);
	}

	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return;
	}

	// Start pjsua
	status = pjsua_start();
	if (status != PJ_SUCCESS) {
		pjsua_destroy();
		return;
	}

	pj_ready = true;

	// Set snd devices
	UpdateSoundDevicesIds();

	//Set aud codecs prio
	if (accountSettings.audioCodecs.IsEmpty())
	{
		accountSettings.audioCodecs = _T(_GLOBAL_CODECS_ENABLED);
	}
	if (accountSettings.audioCodecs.GetLength())
	{
		pjsua_codec_info codec_info[64];
		unsigned count = 64;
		pjsua_enum_codecs(codec_info, &count);
		for (unsigned i=0;i<count;i++) {
			pjsua_codec_set_priority(&codec_info[i].codec_id, PJMEDIA_CODEC_PRIO_DISABLED);
			CString rab = PjToStr(&codec_info[i].codec_id);
			if (!audioCodecList.Find(rab)) {
				audioCodecList.AddTail(rab);
				rab.Append(_T("~"));
				audioCodecList.AddTail(rab);
			}
		}
		POSITION pos = audioCodecList.GetHeadPosition();
		while (pos) {
			POSITION posKey = pos;
			CString key = audioCodecList.GetNext(pos);
			POSITION posValue = pos;
			CString value = audioCodecList.GetNext(pos);
			pj_str_t codec_id = StrToPjStr(key);
			pjmedia_codec_param param;
			if (pjsua_codec_get_param(&codec_id, &param)!=PJ_SUCCESS) {
				audioCodecList.RemoveAt(posKey);
				audioCodecList.RemoveAt(posValue);
			}
		};

		int curPos = 0;
		int i = PJMEDIA_CODEC_PRIO_NORMAL;
		CString resToken = accountSettings.audioCodecs.Tokenize(_T(" "),curPos);
		while (!resToken.IsEmpty()) {
			pj_str_t codec_id = StrToPjStr(resToken);
			/*
			if (pj_strcmp2(&codec_id,"opus/48000/2") == 0) {
				pjmedia_codec_param param;
				pjsua_codec_get_param(&codec_id, &param);
				//param.info.clock_rate = 16000;
				param.info.avg_bps = 16000;
				param.info.max_bps = param.info.avg_bps * 2;
				//param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].name=pj_str("maxplaybackrate");
				//param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].val = pj_str("8000");
				//param.setting.dec_fmtp.cnt++;
				param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].name=pj_str("sprop-maxcapturerate");
				param.setting.dec_fmtp.param[param.setting.dec_fmtp.cnt].val = pj_str("8000");
				param.setting.dec_fmtp.cnt++;
				pjsua_codec_set_param(&codec_id, &param);
			}
			*/
			pjsua_codec_set_priority(&codec_id, i);
					resToken= accountSettings.audioCodecs.Tokenize(_T(" "),curPos);
			i--;
		}
	}

#ifdef _GLOBAL_VIDEO
	//Set vid codecs prio
	if (accountSettings.videoCodec.GetLength())
	{
		pj_str_t codec_id = StrToPjStr(accountSettings.videoCodec);
		pjsua_vid_codec_set_priority(&codec_id,255);
	}
	int bitrate;
	if (accountSettings.disableH264) {
		pjsua_vid_codec_set_priority(&pj_str("H264"),0);
	} else
	{
		const pj_str_t codec_id = {"H264", 4};
		pjmedia_vid_codec_param param;
		pjsua_vid_codec_get_param(&codec_id, &param);
		if (atoi(CStringA(accountSettings.bitrateH264))) {
			bitrate = 1000 * atoi(CStringA(accountSettings.bitrateH264));
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
		}
		/*
		param.enc_fmt.det.vid.size.w = 140;
		param.enc_fmt.det.vid.size.h = 80;
		param.enc_fmt.det.vid.fps.num = 30;
		param.enc_fmt.det.vid.fps.denum = 1;
		param.dec_fmt.det.vid.size.w = 640;
		param.dec_fmt.det.vid.size.h = 480;
		param.dec_fmt.det.vid.fps.num = 30;
		param.dec_fmt.det.vid.fps.denum = 1;
		*/
		/*
		// Defaut (level 1e, 30):
		param.dec_fmtp.cnt = 2;
		param.dec_fmtp.param[0].name = pj_str("profile-level-id");
		param.dec_fmtp.param[0].val = pj_str("42e01e");
		param.dec_fmtp.param[1].name = pj_str("packetization-mode");
		param.dec_fmtp.param[1].val = pj_str("1");
		//*/
		pjsua_vid_codec_set_param(&codec_id, &param);
	}
	if (accountSettings.disableH263) {
		pjsua_vid_codec_set_priority(&pj_str("H263"),0);
	} else {
		if (atoi(CStringA(accountSettings.bitrateH263))) {
			const pj_str_t codec_id = {"H263", 4};
			pjmedia_vid_codec_param param;
			pjsua_vid_codec_get_param(&codec_id, &param);
			bitrate = 1000 * atoi(CStringA(accountSettings.bitrateH263));
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
			pjsua_vid_codec_set_param(&codec_id, &param);
		}
	}
	if (accountSettings.disableVP8) {
		pjsua_vid_codec_set_priority(&pj_str("VP8"),0);
	} else {
		if (atoi(CStringA(accountSettings.bitrateVP8))) {
			const pj_str_t codec_id = {"VP8", 4};
			pjmedia_vid_codec_param param;
			pjsua_vid_codec_get_param(&codec_id, &param);
			bitrate = 1000 * atoi(CStringA(accountSettings.bitrateVP8));
			param.enc_fmt.det.vid.avg_bps = bitrate;
			param.enc_fmt.det.vid.max_bps = bitrate;
			pjsua_vid_codec_set_param(&codec_id, &param);
		}
	}
#endif

	// Create transport
	transport_udp_local = -1;
	transport_udp = -1;
	transport_tcp = -1;
	transport_tls = -1;

	pjsua_transport_config_default(&cfg);
	cfg.public_addr = StrToPjStr( accountSettings.account.publicAddr );

	if (accountSettings.sourcePort) {
		cfg.port=accountSettings.sourcePort;
		status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		if (status != PJ_SUCCESS) {
			cfg.port=0;
			pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		} 
		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			if (accountSettings.sourcePort == 5060 ) {
				transport_udp_local = transport_udp;
			} else {
				cfg.port=5060;
				status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp_local);
				if (status != PJ_SUCCESS) {
					transport_udp_local = transport_udp;
				}
			}
		}
	} else {
		if (MACRO_ENABLE_LOCAL_ACCOUNT) {
			cfg.port=5060;
			status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp_local);
			if (status != PJ_SUCCESS) {
				transport_udp_local = -1;
			}
		}
		cfg.port=0;
		pjsua_transport_create(PJSIP_TRANSPORT_UDP, &cfg, &transport_udp);
		if (transport_udp_local == -1) {
			transport_udp_local = transport_udp;
		}
	}

	cfg.port = MACRO_ENABLE_LOCAL_ACCOUNT ? 5060 : 0;
	status = pjsua_transport_create(PJSIP_TRANSPORT_TCP, &cfg, &transport_tcp);
	if (status != PJ_SUCCESS && cfg.port) {
		cfg.port=0;
		pjsua_transport_create(PJSIP_TRANSPORT_TCP, &cfg, &transport_tcp);
	}

	cfg.port = MACRO_ENABLE_LOCAL_ACCOUNT ? 5061 : 0;
	status = pjsua_transport_create(PJSIP_TRANSPORT_TLS, &cfg, &transport_tls);
	if (status != PJ_SUCCESS && cfg.port) {
		cfg.port=0;
		pjsua_transport_create(PJSIP_TRANSPORT_TLS, &cfg, &transport_tls);
	}

	if (accountSettings.usersDirectory.Find(_T("%s"))==-1 && accountSettings.usersDirectory.Find(_T("{"))==-1) {
		UsersDirectoryLoad();
	}

	account = PJSUA_INVALID_ID;
	account_local = PJSUA_INVALID_ID;

	PJAccountAddLocal();

}

void CmainDlg::UpdateSoundDevicesIds()
{
	audio_input=-1;
	audio_output=-2;
	audio_ring=-2;

	unsigned count = 128;
	pjmedia_aud_dev_info aud_dev_info[128];
	pjsua_enum_aud_devs(aud_dev_info, &count);
	for (unsigned i=0;i<count;i++)
	{
		CString audDevName(aud_dev_info[i].name);
		if (aud_dev_info[i].input_count && !accountSettings.audioInputDevice.Compare(audDevName)) {
			audio_input = i;
		}
		if (aud_dev_info[i].output_count) {
			if (!accountSettings.audioOutputDevice.Compare(audDevName)) {
				audio_output = i;
			}
			if (!accountSettings.audioRingDevice.Compare(audDevName)) {
				audio_ring = i;
			}
		}
	}
}

void CmainDlg::PJDestroy()
{
	if (pj_ready) {
		if (pageContacts) {
			pageContacts->PresenceUnsubsribe();
		}
		call_deinit_tonegen(-1);

		toneCalls.RemoveAll();
		
		if (IsWindow(m_hWnd)) {
			KillTimer(IDT_TIMER_TONE);
		}

		PlayerStop();

		if (accountSettings.accountId) {
			PJAccountDelete();
		}

		pj_ready = false;

		//if (transport_udp_local!=PJSUA_INVALID_ID && transport_udp_local!=transport_udp) {
		//	pjsua_transport_close(transport_udp_local,PJ_TRUE);
		//}
		if (transport_udp!=PJSUA_INVALID_ID) {
			//pjsua_transport_close(transport_udp,PJ_TRUE);
		}
		//if (transport_tcp!=PJSUA_INVALID_ID) {
		//	pjsua_transport_close(transport_tcp,PJ_TRUE);
		//}
		//if (transport_tls!=PJSUA_INVALID_ID) {
		//	pjsua_transport_close(transport_tls,PJ_TRUE);
		//}
		pjsua_destroy();
	}
}

void CmainDlg::PJAccountConfig(pjsua_acc_config *acc_cfg)
{
	pjsua_acc_config_default(acc_cfg);
#ifdef _GLOBAL_VIDEO
	acc_cfg->vid_in_auto_show = PJ_TRUE;
	acc_cfg->vid_out_auto_transmit = PJ_TRUE;
	acc_cfg->vid_cap_dev = VideoCaptureDeviceId();
	acc_cfg->vid_wnd_flags = PJMEDIA_VID_DEV_WND_BORDER | PJMEDIA_VID_DEV_WND_RESIZABLE;
#endif

}

void CmainDlg::PJAccountAdd()
{

	if (!accountSettings.accountId) {
		return;
	}
	if (accountSettings.account.username.IsEmpty()) {
		OnAccount(0,0);
		return;
	}

	CString title = _T(_GLOBAL_NAME_NICE);
	CString usernameLocal;
	usernameLocal = accountSettings.account.username;
	if (!accountSettings.account.label.IsEmpty())
	{
		title.Append( _T(" - ") + accountSettings.account.label);
	} else if (!accountSettings.account.displayName.IsEmpty())
	{
		title.Append( _T(" - ") + accountSettings.account.displayName);
	} else if (!usernameLocal.IsEmpty())
	{
		title.Append( _T(" - ") + usernameLocal);
	}
	SetWindowText(title);

	pjsua_acc_config acc_cfg;
	PJAccountConfig(&acc_cfg);

	if (accountSettings.account.disableSessionTimer) {
		acc_cfg.use_timer = PJSUA_SIP_TIMER_INACTIVE; 
	}

	if (accountSettings.account.srtp ==_T("optional")) {
		acc_cfg.use_srtp = PJMEDIA_SRTP_OPTIONAL;
	} else if (accountSettings.account.srtp ==_T("mandatory")) {
		acc_cfg.use_srtp = PJMEDIA_SRTP_MANDATORY;
	} else {
		acc_cfg.use_srtp = PJMEDIA_SRTP_DISABLED;
	}

	acc_cfg.ice_cfg_use = PJSUA_ICE_CONFIG_USE_CUSTOM;
	acc_cfg.ice_cfg.enable_ice = accountSettings.account.ice ? PJ_TRUE : PJ_FALSE;
	acc_cfg.rtp_cfg.public_addr = StrToPjStr( accountSettings.account.publicAddr );
	acc_cfg.allow_via_rewrite=accountSettings.account.allowRewrite ? PJ_TRUE : PJ_FALSE;
	acc_cfg.allow_sdp_nat_rewrite=acc_cfg.allow_via_rewrite;
	acc_cfg.allow_contact_rewrite=acc_cfg.allow_via_rewrite ? 2 : PJ_FALSE;
	acc_cfg.publish_enabled = accountSettings.account.publish ? PJ_TRUE : PJ_FALSE;

	if (!accountSettings.account.voicemailNumber.IsEmpty()) {
		acc_cfg.mwi_enabled = PJ_TRUE;
	}

	transport = MSIP_TRANSPORT_AUTO;
	if (accountSettings.account.transport==_T("udp") && transport_udp!=-1) {
		acc_cfg.transport_id = transport_udp;
	} else if (accountSettings.account.transport==_T("tcp") && transport_tcp!=-1) {
		transport = MSIP_TRANSPORT_TCP;
	} else if (accountSettings.account.transport==_T("tls") && transport_tls!=-1) {
		transport = MSIP_TRANSPORT_TLS;
	}

	CString str;
	str.Format(_T("%s..."),Translate(_T("Connecting")));
	UpdateWindowText(str);

	CString proxy;
	if (!accountSettings.account.proxy.IsEmpty()) {
		acc_cfg.proxy_cnt = 1;
		proxy.Format(_T("sip:%s"),accountSettings.account.proxy);
		AddTransportSuffix(proxy);
		acc_cfg.proxy[0] = StrToPjStr( proxy );
	}

	CString localURI;
	if (!accountSettings.account.displayName.IsEmpty()) {
		localURI = _T("\"") + accountSettings.account.displayName + _T("\" ");
	}
	localURI += GetSIPURI(get_account_username());
	acc_cfg.id = StrToPjStr(localURI);
	acc_cfg.cred_count = 1;
	acc_cfg.cred_info[0].username = StrToPjStr(!accountSettings.account.authID.IsEmpty()? accountSettings.account.authID : get_account_username());
	acc_cfg.cred_info[0].realm = pj_str("*");
	acc_cfg.cred_info[0].scheme = pj_str("Digest");
	acc_cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	acc_cfg.cred_info[0].data = StrToPjStr( accountSettings.account.password );

	CString regURI;
	if (accountSettings.account.server.IsEmpty()) {
		acc_cfg.register_on_acc_add = PJ_FALSE;
	} else {
		regURI.Format(_T("sip:%s"),accountSettings.account.server);
		AddTransportSuffix(regURI);
		acc_cfg.reg_uri = StrToPjStr( regURI );
	}
	//-- port knocker
	if (!accountSettings.portKnockerPorts.IsEmpty()) {
		CString host;
		CString ip;
		if (!accountSettings.portKnockerHost.IsEmpty()) {
			host = accountSettings.portKnockerHost;
		} else {
			host = RemovePort(accountSettings.account.server);
		}
		if (!host.IsEmpty()) {
			AfxSocketInit();
			if (IsIP(host)) {
				ip = host;
			} else {
				hostent *he = gethostbyname(CStringA(host));
				if (he) {
					ip = inet_ntoa (*((struct in_addr *) he->h_addr_list[0]));
				}
			}
			CSocket udpSocket;
			if (!ip.IsEmpty() && udpSocket.Create(0,SOCK_DGRAM,0)) {
				int pos = 0;
				CString strPort = accountSettings.portKnockerPorts.Tokenize(_T(","),pos);
				while (pos!=-1) {
					strPort.Trim();
					if (!strPort.IsEmpty()) {
						int port = StrToInt(strPort);
						if (port > 0 && port <= 65535) {
							udpSocket.SendToEx(0,0,port,ip);
						}
					}
					strPort = accountSettings.portKnockerPorts.Tokenize(_T(","),pos);
				}
				udpSocket.Close();
			}
		}
	}
	//--
	pj_status_t status = pjsua_acc_add(&acc_cfg, PJ_TRUE, &account);
	if (status == PJ_SUCCESS) {
		PublishStatus(true, acc_cfg.register_on_acc_add && true);
	} else {
		ShowErrorMessage(status);
		UpdateWindowText(_T(""), IDI_DEFAULT, true);
	}
}

void CmainDlg::PJAccountAddLocal()
{
	if (MACRO_ENABLE_LOCAL_ACCOUNT) {
		pj_status_t status;
		pjsua_acc_config acc_cfg;
		PJAccountConfig(&acc_cfg);
		acc_cfg.priority--;
		pjsua_transport_data *t = &pjsua_var.tpdata[0];
		CString localURI;
		localURI.Format(_T("<sip:%s>"), PjToStr(&t->local_name.host));
		acc_cfg.id = StrToPjStr(localURI);
		pjsua_acc_add(&acc_cfg, PJ_TRUE, &account_local);
		acc_cfg.priority++;
	}
}

void CmainDlg::PJAccountDelete()
{
	if (pageContacts) {
		pageContacts->PresenceUnsubsribe();
	}
	if (pjsua_acc_is_valid(account)) {
		pjsua_acc_del(account);
		account = PJSUA_INVALID_ID;
	}
	onMWIInfo(0,0);
	SetWindowText(_T(_GLOBAL_NAME_NICE));
	UpdateWindowText();
}

void CmainDlg::PJAccountDeleteLocal()
{
	if (pjsua_acc_is_valid(account_local)) {
		pjsua_acc_del(account_local);
		account_local = PJSUA_INVALID_ID;
	}
}

void CmainDlg::OnTcnSelchangeTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	CTabCtrl* tab = (CTabCtrl*) GetDlgItem(IDC_TAB);
	int nTab = tab->GetCurSel();
	TC_ITEM tci;
	tci.mask = TCIF_PARAM;
	tab->GetItem(nTab, &tci);
	if (tci.lParam>0) {
		CWnd* pWnd = (CWnd *)tci.lParam;
		if (m_tabPrev!=-1) {
			tab->GetItem(m_tabPrev, &tci);
			if (tci.lParam>0) {
				((CWnd *)tci.lParam)->ShowWindow(SW_HIDE);
			}
		}
		pWnd->ShowWindow(SW_SHOW);
		pWnd->SetFocus();
		if (nTab!=accountSettings.activeTab) {
			accountSettings.activeTab=nTab;
			AccountSettingsPendingSave();
		}
	} else {
		if (tci.lParam == -1) {
			MainPopupMenu();
		}
		else if (tci.lParam == -2) {
			OpenURL(_T(_GLOBAL_TAB_HELP));
		}
		if (m_tabPrev!=-1) {
			tab->SetCurSel(m_tabPrev);
			tab->Invalidate();
		}
	}
	*pResult = 0;
}

void CmainDlg::OnTcnSelchangingTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	CTabCtrl* tab = (CTabCtrl*) GetDlgItem(IDC_TAB);
	m_tabPrev = tab->GetCurSel();
	*pResult = FALSE;
}

void CmainDlg::UpdateWindowText(CString text, int icon, bool afterRegister)
{
	if (text.IsEmpty() && pjsua_call_get_count()>0) {
		return;
	}
	CString str;
	bool showAccountDlg = false;
	if (text.IsEmpty() || text==_T("-")) {
		if (pjsua_acc_is_valid(account)) {
			pjsua_acc_info info;
			pjsua_acc_get_info(account,&info);
			str = PjToStr(&info.status_text);
			if ( str != _T("Default status message") ) {
				if (!info.has_registration || str==_T("OK"))
				{
					if (m_isAway) {
						icon = IDI_AWAY;
						str = Translate(_T("Away"));
					} else {
						if (info.has_registration && transport == MSIP_TRANSPORT_TLS) {
							icon = IDI_SECURE;
							//str = Translate(_T("Security"));
						} else {
							icon = IDI_ONLINE;
						}
						str = Translate(_T("Online"));
					}
					if (!info.has_registration) {
						str.AppendFormat(_T(" (%s)"), Translate(_T("outgoing")));
					}
					pageContacts->PresenceSubsribe();
					if (!dialNumberDelayed.IsEmpty())
					{
						DialNumber(dialNumberDelayed);
						dialNumberDelayed=_T("");
					}
				} else if (str == _T("In Progress")) {
					str.Format(_T("%s..."),Translate(_T("Connecting")));
				} else if (info.status == 401 || info.status == 403) {
					onTrayNotify(NULL,WM_LBUTTONUP);
					if (afterRegister) {
						showAccountDlg = true;
					}
					icon = IDI_OFFLINE;
					str = Translate(_T("Incorrect Password"));
				} else {
					str = Translate(str.GetBuffer());
				}
				str = str.GetBuffer();
			} else {
				str.Format( _T("%s: %d"), Translate(_T("Response Code")),info.status );
			}
		} else {
			if (afterRegister) {
				showAccountDlg = true;
			}
			str = _T(_GLOBAL_NAME_NICE);
			icon = IDI_DEFAULT;
		}
	} else
	{
		str = text;
	}

	CString* pPaneText = new CString();
	*pPaneText = str;
	PostMessage(UM_SET_PANE_TEXT, NULL, (LPARAM)pPaneText);

	if (icon!=-1) {
		HICON hIcon = (HICON)LoadImage(
			AfxGetInstanceHandle(),
			MAKEINTRESOURCE(icon),
			IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED );
		m_bar.GetStatusBarCtrl().SetIcon(0, hIcon);
		iconStatusbar = icon;
		
		tnd.uFlags = tnd.uFlags & ~NIF_INFO;
		if ((!pjsua_acc_is_valid(account) && MACRO_ENABLE_LOCAL_ACCOUNT) || (icon!=IDI_DEFAULT && icon!=IDI_OFFLINE)) {
			if (tnd.hIcon != iconSmall) {
				tnd.hIcon = iconSmall;
				Shell_NotifyIcon(NIM_MODIFY,&tnd);
			}
		} else {
			if (tnd.hIcon != iconInactive) {
				tnd.hIcon = iconInactive;
				Shell_NotifyIcon(NIM_MODIFY,&tnd);
			}
		}
	}
	if (showAccountDlg) {
		PostMessage(UM_ON_ACCOUNT,1);
	}
}

void CmainDlg::PublishStatus(bool online, bool init)
{
	if (pjsua_acc_is_valid(account))
	{
		pjrpid_element pr;
		pr.type = PJRPID_ELEMENT_TYPE_PERSON;
		pr.id = pj_str(NULL);
		pr.note = pj_str(NULL);
		pr.note = online ? pj_str("Idle") : pj_str("Away");
		pr.activity = online ? PJRPID_ACTIVITY_UNKNOWN : PJRPID_ACTIVITY_AWAY;
		pjsua_acc_set_online_status2(account, PJ_TRUE, &pr);
		m_isAway = !online;
		if (!init) {
			UpdateWindowText();
		}
	}
}

LRESULT CmainDlg::onCopyData(WPARAM wParam,LPARAM lParam)
{
	if (pj_ready) {
		COPYDATASTRUCT *s = (COPYDATASTRUCT*)lParam;
		if (s && s->dwData == 1 ) {
			CString params = (LPCTSTR)s->lpData;
			params.Trim();
			if (!params.IsEmpty()) {
				if (params == _T("/answer")) {
					msip_call_answer();
				} else if (params == _T("/hangupall")) {
					call_hangup_all_noincoming();
				} else {
					DialNumberFromCommandLine(params);
				}
			}
		}
	}
	return 0;
}

void CmainDlg::GotoTab(int i, CTabCtrl* tab) {
	if (!tab) {
		tab = (CTabCtrl*) GetDlgItem(IDC_TAB);
	}
	int nTab = tab->GetCurSel();
	if (nTab != i) {
		LRESULT pResult;
		mainDlg->OnTcnSelchangingTab(NULL, &pResult);
		tab->SetCurSel(i);
		mainDlg->OnTcnSelchangeTab(NULL, &pResult);
	}
}


void CmainDlg::DialNumberFromCommandLine(CString number) {
	GotoTab(0);
	pjsua_acc_info info;
	number.Trim('"');
	if (number.Mid(0,4).CompareNoCase(_T("tel:"))==0 || number.Mid(0,4).CompareNoCase(_T("sip:"))==0) {
		number = number.Mid(4);
	} else if (number.Mid(0,7).CompareNoCase(_T("callto:"))==0) {
		number = number.Mid(7);
	}
	if (accountSettings.accountId>0) {
		if (pjsua_acc_is_valid(account) && 
				(accountSettings.account.server.IsEmpty() ||
					(pjsua_acc_get_info(account,&info)==PJ_SUCCESS && info.status==200)
				)
			){
				DialNumber(number);
		} else {
			dialNumberDelayed = number;
		}
	} else {
		if (pjsua_acc_is_valid(account_local)) {
			DialNumber(number);
		} else if (accountSettings.enableLocalAccount) {
			dialNumberDelayed = number;
		}
	}
}

void CmainDlg::DialNumber(CString params)
{
	CString number;
	CString message;
	int i = params.Find(_T(" "));
	if (i!=-1) {
		number = params.Mid(0,i);
		message = params.Mid(i+1);
		message.Trim();
	} else {
		number = params;
	}
	number.Trim();
	if (!number.IsEmpty()) {
		if (message.IsEmpty()) {
			MakeCall(number);
		} else {
			messagesDlg->SendMessage(NULL, message, number);
		}
	}
}

void CmainDlg::MakeCall(CString number, bool hasVideo)
{
	if (accountSettings.singleMode && call_get_count_noincoming()) {
		mainDlg->GotoTab(0);
		return;
	}
	number.Trim();
	pageDialer->SetNumber(number);
#ifdef _GLOBAL_VIDEO
		if (hasVideo) {
			pageDialer->OnBnClickedVideoCall();
		} else {
			pageDialer->OnBnClickedCall();
		}
#else 
		pageDialer->OnBnClickedCall();
#endif
}

void CmainDlg::AutoAnswer(pjsua_call_id call_id)
{
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id, &call_info) != PJ_SUCCESS || (call_info.state != PJSIP_INV_STATE_INCOMING && call_info.state != PJSIP_INV_STATE_EARLY)) {
		return;
	}
	mainDlg->PostMessage(UM_CALL_ANSWER, (WPARAM)call_id, (LPARAM)call_info.rem_vid_cnt);
	if (!accountSettings.hidden) {
		mainDlg->PostMessage(UM_ON_PLAYER_PLAY,MSIP_SOUND_RINGIN2,0);
	}
}

LRESULT CmainDlg::onPlayerPlay(WPARAM wParam,LPARAM lParam)
{
	CString filename;
	BOOL noLoop;
	BOOL inCall;
	if (wParam == MSIP_SOUND_CUSTOM) {
		filename = *(CString*)lParam;
		noLoop = FALSE;
		inCall = FALSE;
	} else {
		filename = accountSettings.pathExe + _T("\\");
		switch(wParam){
		case MSIP_SOUND_MESSAGE_IN:
			filename.Append(_T("messagein.wav"));
			noLoop = TRUE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_MESSAGE_OUT:
			filename.Append(_T("messageout.wav"));
			noLoop = TRUE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_HANGUP:
			filename.Append(_T("hangup.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		case MSIP_SOUND_RINGIN:
			filename.Append(_T("ringin.wav"));
			noLoop = FALSE;
			inCall = FALSE;
			break;
		case MSIP_SOUND_RINGIN2:
			filename.Append(_T("ringin2.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		case MSIP_SOUND_RINGOUT:
			filename.Append(_T("ringout.wav"));
			noLoop = TRUE;
			inCall = TRUE;
			break;
		default:
			noLoop = TRUE;
			inCall = FALSE;
		}
	}
	PlayerPlay(filename, noLoop, inCall);
	return 0;
}

LRESULT CmainDlg::onPlayerStop(WPARAM wParam,LPARAM lParam)
{
	PlayerStop();
	return 0;
}


struct pjsua_player_eof_data
{
	pj_pool_t          *pool;
	pjsua_player_id player_id;
};

static PJ_DEF(pj_status_t) on_pjsua_wav_file_end_callback(pjmedia_port* media_port, void* args)
{
	mainDlg->PostMessage(UM_ON_PLAYER_STOP,0,0);
	return -1;//Here it is important to return value other than PJ_SUCCESS
}

void CmainDlg::PlayerPlay(CString filename, bool noLoop, bool inCall)
{
	PlayerStop();
	if (!filename.IsEmpty()) {
		pj_str_t file = StrToPjStr(filename);
		if (pjsua_var.mconf && pjsua_player_create( &file, noLoop?PJMEDIA_FILE_NO_LOOP:0, &player_id)== PJ_SUCCESS) {
			pjmedia_port *player_media_port;
			if ( pjsua_player_get_port(player_id, &player_media_port) == PJ_SUCCESS ) {
				if (noLoop) {
					pj_pool_t *pool = pjsua_pool_create("microsip_eof_data", 512, 512);
					struct pjsua_player_eof_data *eof_data = PJ_POOL_ZALLOC_T(pool, struct pjsua_player_eof_data);
					eof_data->pool = pool;
					eof_data->player_id = player_id;
					pjmedia_wav_player_set_eof_cb(player_media_port, eof_data, &on_pjsua_wav_file_end_callback);
				}
				if (
					(!tone_gen && pjsua_conf_get_active_ports()<=2)
					||
					(tone_gen && pjsua_conf_get_active_ports()<=3)
					) {
						SetSoundDevice(inCall?audio_output:audio_ring);
				}
				pjsua_conf_connect(pjsua_player_get_conf_port(player_id),0);
			}
		}
	}
}

void CmainDlg::PlayerStop()
{
	if (player_id != PJSUA_INVALID_ID) {
		pjsua_conf_disconnect(pjsua_player_get_conf_port(player_id), 0);
		if (pjsua_player_destroy(player_id)==PJ_SUCCESS) {
			player_id = PJSUA_INVALID_ID;
		}
	}
}

void CmainDlg::SetSoundDevice(int outDev, bool forse){
	int in,out;
	if (forse || pjsua_get_snd_dev(&in,&out)!=PJ_SUCCESS || audio_input!=in || outDev!=out ) {
		pjsua_set_snd_dev(audio_input, outDev);
	}
}

LRESULT CmainDlg::onCallAnswer(WPARAM wParam,LPARAM lParam)
{
	pjsua_call_id call_id = wParam;
	if (lParam<0) {
		pjsua_call_answer(call_id, -lParam, NULL, NULL);
		return 0;
	}
	pjsua_call_info call_info;
	if (pjsua_call_get_info(call_id,&call_info)==PJ_SUCCESS) {
		if (call_info.role==PJSIP_ROLE_UAS && (call_info.state == PJSIP_INV_STATE_INCOMING || call_info.state == PJSIP_INV_STATE_EARLY)) {
			if (accountSettings.singleMode) {
				call_hangup_all_noincoming();
			}
			SetSoundDevice(audio_output);
#ifdef _GLOBAL_VIDEO
			if (lParam>0) {
				createPreviewWin();
			}
#endif
			pjsua_call_setting call_setting;
			pjsua_call_setting_default(&call_setting);
			call_setting.vid_cnt=lParam>0 ? 1:0;

			if (pjsua_call_answer2(call_id, &call_setting, 200, NULL, NULL) == PJ_SUCCESS) {
				callIdIncomingIgnore = PjToStr(&call_info.call_id);
			}
			PlayerStop();
			if (!accountSettings.silent) {
				onTrayNotify(NULL,WM_LBUTTONUP);
			}
		}
	}
	return 0;
}

LRESULT CmainDlg::onCallHangup(WPARAM wParam,LPARAM lParam)
{
	pjsua_call_id call_id = wParam;
	msip_call_hangup_fast(call_id);
	return 0;
}

LRESULT CmainDlg::onTabIconUpdate(WPARAM wParam,LPARAM lParam)
{
	if (messagesDlg) {
		pjsua_call_id call_id = wParam;
		for (int i = 0; i < messagesDlg->tab->GetItemCount(); i++) {
			MessagesContact* messagesContact = messagesDlg->GetMessageContact(i);
			if (messagesContact->callId == call_id) {
				pjsua_call_info call_info;
				if (pjsua_call_get_info(call_id, &call_info) == PJ_SUCCESS) {
					messagesDlg->UpdateTabIcon(messagesContact,i,&call_info);	
				}
				break;
			}
		}
	}
	return 0;
}

LRESULT CmainDlg::onSetPaneText(WPARAM wParam,LPARAM lParam)
{
	CString* pString = (CString*)lParam;
    ASSERT(pString != NULL);
    m_bar.SetPaneText(0, *pString);
    delete pString;
	return 0;
}

BOOL CmainDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) 
{
	if (pWnd == GetDlgItem(IDC_VOICEMAIL)) {
           ::SetCursor(m_hCursorHand);
           return TRUE;
    }
	return CBaseDialog::OnSetCursor(pWnd, nHitTest, message);
}

BOOL CmainDlg::CopyStringToClipboard( IN const CString & str )
{
	// Open the clipboard
	if ( !OpenClipboard() )
		return FALSE;

	// Empty the clipboard
	if ( !EmptyClipboard() )
	{
		CloseClipboard();
		return FALSE;
	}

	// Number of bytes to copy (consider +1 for end-of-string, and
	// properly scale byte size to sizeof(TCHAR))
	SIZE_T textCopySize = (str.GetLength() + 1) * sizeof(TCHAR);

	// Allocate a global memory object for the text
	HGLOBAL hTextCopy = GlobalAlloc( GMEM_MOVEABLE, textCopySize );
	if ( hTextCopy == NULL )
	{
		CloseClipboard();
		return FALSE;
	}

	// Lock the handle, and copy source text to the buffer
	TCHAR * textCopy = reinterpret_cast< TCHAR *>( GlobalLock(
		hTextCopy ) );
	ASSERT( textCopy != NULL );
	StringCbCopy( textCopy, textCopySize, str.GetString() );
	GlobalUnlock( hTextCopy );
	textCopy = NULL; // avoid dangling references

	// Place the handle on the clipboard
#if defined( _UNICODE )
	UINT textFormat = CF_UNICODETEXT;  // Unicode text
#else
	UINT textFormat = CF_TEXT;         // ANSI text
#endif // defined( _UNICODE )

	if (SetClipboardData( textFormat, hTextCopy ) == NULL )
	{
		// Failed
		CloseClipboard();
		return FALSE;
	}

	// Release the clipboard
	CloseClipboard();

	// All right
	return TRUE;
}

void CmainDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (nID==SC_CLOSE) {
		ShowWindow(SW_HIDE);
	} else {
		__super::OnSysCommand(nID, lParam);
	}
}

BOOL CmainDlg::OnQueryEndSession()
{
	DestroyWindow();
	return TRUE;
}

void CmainDlg::OnClose()
{
	DestroyWindow ();
}

void CmainDlg::OnContextMenu(CWnd *pWnd, CPoint point)
{
	CPoint local = point;
	ScreenToClient(&local);
	CRect rect;
	GetClientRect(&rect);
	if (rect.Height()-local.y <= 16) {
		MainPopupMenu();
	} else {
		DefWindowProc(WM_CONTEXTMENU,NULL,MAKELPARAM(point.x,point.y));
	}
}

BOOL CmainDlg::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	if (nEventType==DBT_DEVNODES_CHANGED) {
		if (pj_ready) {
			if (dwData==1) {
				PJ_LOG(3, (THIS_FILE, "OnDeviceStateChanged event, schedule refresh devices"));
			} else {
				PJ_LOG(3, (THIS_FILE, "WM_DEVICECHANGE received, schedule refresh devices"));
			}
			KillTimer(IDT_TIMER_SWITCH_DEVICES);
			SetTimer(IDT_TIMER_SWITCH_DEVICES,1500,NULL);
		}
	}
	return FALSE;
}

void CmainDlg::OnSessionChange(UINT nSessionState, UINT nId)
{
	if (nSessionState == WTS_REMOTE_CONNECT || nSessionState == WTS_CONSOLE_CONNECT) {
		if (pj_ready) {
			PJ_LOG(3, (THIS_FILE, "WM_WTSSESSION_CHANGE received, schedule refresh devices"));
			KillTimer(IDT_TIMER_SWITCH_DEVICES);
			SetTimer(IDT_TIMER_SWITCH_DEVICES,1500,NULL);
		}
	}
}

void CmainDlg::OnMove(int x, int y)
{
	if (IsWindowVisible() && !IsZoomed() && !IsIconic()) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.mainX = cRect.left;
		accountSettings.mainY = cRect.top;
		AccountSettingsPendingSave();
	}
}

void CmainDlg::OnSize(UINT type, int w, int h)
{
	CBaseDialog::OnSize(type, w, h);
	if (this->IsWindowVisible() && type == SIZE_RESTORED) {
		CRect cRect;
		GetWindowRect(&cRect);
		accountSettings.mainW = cRect.Width();
		accountSettings.mainH = cRect.Height();
		AccountSettingsPendingSave();
	}
}

void CmainDlg::SetupJumpList()
{
JumpList jl(_T(_GLOBAL_NAME_NICE));
jl.AddTasks();
}

void CmainDlg::RemoveJumpList()
{
JumpList jl(_T(_GLOBAL_NAME_NICE));
jl.DeleteJumpList();
}

void CmainDlg::OnMenuWebsite()
{
	OpenURL(_T("http://www.microsip.org/"));
}

void CmainDlg::OnMenuDonate()
{
	OpenURL(_T("http://www.microsip.org/donate"));
}

LRESULT CmainDlg::onUsersDirectoryLoaded(WPARAM wParam,LPARAM lParam)
{
	URLGetAsyncData *response = (URLGetAsyncData *)wParam;
	int expires = 0;
	if (response->statusCode == 200 && !response->body.IsEmpty()) {
		CXMLFile xmlFile;
		if ( xmlFile.LoadFromStream((BYTE*)response->body.GetBuffer(),response->body.GetLength()) ){
			BOOL ok = FALSE;
			bool changed = false;
			pageContacts->SetCanditates();
			CXMLElement *root = xmlFile.GetRoot();
			CXMLElement *directory = root->GetFirstChild();
			while (directory) {
				if (directory->GetElementType() == XET_TAG) {
					CXMLElement *entry = directory->GetFirstChild();
					while (entry) {
						if (entry->GetElementType() == XET_TAG) {
							CXMLElement *data = entry->GetFirstChild();
							CString number;
							CString name;
							char presence = -1;
							while (data) {
								if (data->GetElementType() == XET_TAG) {
									CString dataName = data->GetElementName();
									CXMLElement *value = data->GetFirstChild();
									if (value->GetElementType() == XET_TEXT) {
										if (dataName.CompareNoCase(_T("name"))==0) {
											name = Utf8DecodeUni(UnicodeToAnsi(value->GetElementName()));
										} else if (dataName.CompareNoCase(_T("extension"))==0 || dataName.CompareNoCase(_T("number"))==0 || dataName.CompareNoCase(_T("telephone"))==0) {
											number = Utf8DecodeUni(UnicodeToAnsi(value->GetElementName()));
										} else if (dataName.CompareNoCase(_T("presence"))==0) {
											CString rab = value->GetElementName();
											presence = !(rab.IsEmpty() || rab == _T("0")
												|| rab.CompareNoCase(_T("no"))==0
												|| rab.CompareNoCase(_T("false"))==0
												|| rab.CompareNoCase(_T("null"))==0);
										}
									}
								}
								data = entry->GetNextChild();
							}
							if (!number.IsEmpty()) {
								if (pageContacts->ContactAdd(number, name, presence, 1, FALSE, TRUE) && !changed) {
									changed = true;
								}
								if (!ok) {
									ok = TRUE;
								}
							}
						}
						entry = directory->GetNextChild();
					}
				}
				directory=root->GetNextChild();
			}
			if (ok) {
				if (pageContacts->DeleteCanditates() || changed) {
					pageContacts->ContactsSave();
				}
			}
		}
		response->headers.MakeLower();
		CString search = _T("\r\ncache-control:");
		int n = response->headers.Find(search);
		if (n>0) {
			n = n+search.GetLength();
			int l = response->headers.Find(_T("\r\n"),n);
			if (l>0) {
				response->headers = response->headers.Mid(n,l-n);
				search = _T("max-age=");
				n = response->headers.Find(search);
				if (n!=-1) {
					response->headers = response->headers.Mid(n+search.GetLength());
					expires = atoi(CStringA(response->headers));
				}
			}
		}
	}
	if (expires<=0) {
		expires = 3600;
	} else if (expires<60) {
		expires = 60;
	} else if (expires>86400) {
		expires = 86400;
	}
	SetTimer(IDT_TIMER_DIRECTORY,1000*expires,NULL);

	PJ_LOG(3, (THIS_FILE, "End UsersDirectoryLoad"));
	return 0;
}

void CmainDlg::UsersDirectoryLoad()
{
	KillTimer(IDT_TIMER_DIRECTORY);
	if (!accountSettings.usersDirectory.IsEmpty()) {
		CString url;
		url.Format(accountSettings.usersDirectory,accountSettings.account.username,accountSettings.account.password,accountSettings.account.server);
		url = msip_url_mask(url);
		PJ_LOG(3, (THIS_FILE, "Begin UsersDirectoryLoad"));
		URLGetAsync(url,m_hWnd,UM_USERS_DIRECTORY);
	}
}

void CmainDlg::AccountSettingsPendingSave()
{
	KillTimer(IDT_TIMER_SAVE);
	SetTimer(IDT_TIMER_SAVE,5000,NULL);
}

void CmainDlg::CheckUpdates()
{
	CInternetSession session;
	try {
		CHttpFile* pFile;
		CString url = _T("http://update.microsip.org/?version=");
		url.Append(_T(_GLOBAL_VERSION));
#ifndef _GLOBAL_VIDEO
		url.Append(_T("&lite=1"));
#endif
		pFile = (CHttpFile*)session.OpenURL(url, NULL, INTERNET_FLAG_TRANSFER_ASCII | INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE);
		if (pFile)
		{
			DWORD dwStatusCode;
			pFile->QueryInfoStatusCode(dwStatusCode);
			if (dwStatusCode == 202) {
				if (::MessageBox(this->m_hWnd, Translate(_T("Do you want to update now?")), Translate(_T("Update Available")), MB_YESNO|MB_ICONQUESTION) == IDYES)
				{
					OpenURL(_T("http://www.microsip.org/downloads"));
				}
			}
			pFile->Close();
		}
		session.Close();
	} catch (CInternetException *e) {}
}

#ifdef _GLOBAL_VIDEO
int CmainDlg::VideoCaptureDeviceId(CString name)
{
	unsigned count = 64;
	pjmedia_vid_dev_info vid_dev_info[64];
	pjsua_vid_enum_devs(vid_dev_info, &count);
	for (unsigned i=0;i<count;i++) {
		if (vid_dev_info[i].fmt_cnt && (vid_dev_info[i].dir==PJMEDIA_DIR_ENCODING || vid_dev_info[i].dir==PJMEDIA_DIR_ENCODING_DECODING)) {
			CString vidDevName(vid_dev_info[i].name);
			if ((!name.IsEmpty() && name == vidDevName)
				||
				(name.IsEmpty() && accountSettings.videoCaptureDevice == vidDevName)) {
				return vid_dev_info[i].id;
			}
		}
	}
	return PJMEDIA_VID_DEFAULT_CAPTURE_DEV;
}

void CmainDlg::createPreviewWin()
{
	if (!previewWin) {
		previewWin = new Preview(this);
	}
	previewWin->Start(VideoCaptureDeviceId());
}
#endif
