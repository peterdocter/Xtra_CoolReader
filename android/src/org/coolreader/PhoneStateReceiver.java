package org.coolreader;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.telephony.PhoneStateListener;
import android.telephony.TelephonyManager;
import android.util.Log;

public class PhoneStateReceiver extends BroadcastReceiver {

	public static class CustomPhoneStateListener extends PhoneStateListener {

		int lastState = -1;
		@Override
		public void onCallStateChanged(int state, String incomingNumber){
		        if (state == lastState)
		        	return;
		        lastState = state;
		        switch(state){
	                case TelephonyManager.CALL_STATE_IDLE:
	                    break;
	                case TelephonyManager.CALL_STATE_RINGING:
                        if (onPhoneActivityStartedHandler != null)
                        	onPhoneActivityStartedHandler.run();
                        break;
	                case TelephonyManager.CALL_STATE_OFFHOOK:
                        if (onPhoneActivityStartedHandler != null)
                        	onPhoneActivityStartedHandler.run();
                        break;
		        }       
		}
	}
	
	@Override
	public void onReceive(Context context, Intent intent) {
	    TelephonyManager telephony = (TelephonyManager)context.getSystemService(Context.TELEPHONY_SERVICE);
        CustomPhoneStateListener customPhoneListener = new CustomPhoneStateListener();

        telephony.listen(customPhoneListener, PhoneStateListener.LISTEN_CALL_STATE);
	}
	
	public static void setPhoneActivityHandler(Runnable handler) {
		onPhoneActivityStartedHandler = handler;
	}
	private static Runnable onPhoneActivityStartedHandler;

}
