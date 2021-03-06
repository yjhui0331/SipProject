// pjsua2-test.cpp: 定义控制台应用程序的入口点。
//

#include "stdafx.h"

#include <iostream>


#pragma comment(lib,"ws2_32.lib")  
#pragma comment(lib,"wsock32.lib")  
#pragma comment(lib,"ole32.lib")  
#pragma comment(lib,"dsound.lib")

#include <pjsua2.hpp>

using namespace pj;

#ifdef _DEBUG
#pragma comment(lib, "libpjproject-i386-Win32-vc14-Debug.lib")
#else
#pragma comment(lib, "libpjproject-i386-Win32-vc14-Release.lib")
#endif

// Subclass to extend the Account and get notifications etc.
class MyAccount : public Account
{
public:
	virtual void onRegState(OnRegStateParam &prm)
	{
		AccountInfo ai = getInfo();
		std::cout << (ai.regIsActive ? "*** Register:" : "*** Unregister:")
			<< " code=" << prm.code << std::endl;
	}
};



int main()
{
	Endpoint ep;

	ep.libCreate();

	// Initialize endpoint
	EpConfig ep_cfg;
	ep.libInit(ep_cfg);

	// Create SIP transport. Error handling sample is shown
	TransportConfig tcfg;
	tcfg.port = 5060;
	try
	{
		ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);
	}
	catch (Error &err)
	{
		std::cout << err.info() << std::endl;
		return 1;
	}

	// Start the library (worker threads etc)
	ep.libStart();
	std::cout << "*** PJSUA2 STARTED ***" << std::endl;

	// Configure an AccountConfig
	AccountConfig acfg;
	acfg.idUri = "sip:5011@192.168.50.200";
	acfg.regConfig.registrarUri = "sip:5011@192.168.50.200";
	AuthCredInfo cred("digest", "*", "5011", 0, "5011");
	acfg.sipConfig.authCreds.push_back(cred);

	// Create the account
	MyAccount *acc = new MyAccount;
	acc->create(acfg);

	// Here we don't have anything else to do..
	pj_thread_sleep(10000);

	// Delete the account. This will unregister from server
	delete acc;

    return 0;
}

