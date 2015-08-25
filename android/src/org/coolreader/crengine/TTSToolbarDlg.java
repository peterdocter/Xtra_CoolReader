package org.coolreader.crengine;

import java.util.HashMap;

import org.coolreader.CoolReader;
import org.coolreader.R;

import android.graphics.drawable.BitmapDrawable;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.View.OnKeyListener;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.ImageButton;
import android.widget.PopupWindow;
import android.widget.PopupWindow.OnDismissListener;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;

public class TTSToolbarDlg implements TTS.OnUtteranceCompletedListener {
	PopupWindow mWindow;
	View mAnchor;
	CoolReader mCoolReader;
	ReaderView mReaderView;
	View mPanel;
	TTS mTTS;
	ImageButton playPauseButton; 
	SeekBar sbSpeed;
	SeekBar sbVolume;
	
	static public TTSToolbarDlg showDialog( CoolReader coolReader, ReaderView readerView, TTS tts)
	{
		TTSToolbarDlg dlg = new TTSToolbarDlg(coolReader, readerView, tts);
		return dlg;
	}
	
	private Runnable onCloseListener;
	public void setOnCloseListener(Runnable handler) {
		onCloseListener = handler;
	}

	private boolean closed; 
	public void stopAndClose() {
		if (closed)
			return;
		isSpeaking = false;
		closed = true;
		BackgroundThread.instance().executeGUI(new Runnable() {
			@Override
			public void run() {
				stop();
				restoreReaderMode();
				mReaderView.clearSelection();
				if (onCloseListener != null)
					onCloseListener.run();
				if ( mWindow.isShowing() )
					mWindow.dismiss();
				mReaderView.save();
			}
		});
	}
	
	private boolean changedPageMode;
	private void setReaderMode()
	{
		String oldViewSetting = mReaderView.getSetting( ReaderView.PROP_PAGE_VIEW_MODE );
		if ( "1".equals(oldViewSetting) ) {
			changedPageMode = true;
			mReaderView.setSetting(ReaderView.PROP_PAGE_VIEW_MODE, "0");
		}
		moveSelection( ReaderCommand.DCMD_SELECT_FIRST_SENTENCE );
	}
	
	private void restoreReaderMode()
	{
		if ( changedPageMode ) {
			mReaderView.setSetting(ReaderView.PROP_PAGE_VIEW_MODE, "1");
		}
	}
	
	private Selection currentSelection;
	
	private void moveSelection( ReaderCommand cmd )
	{
		mReaderView.moveSelection(cmd, 0, new ReaderView.MoveSelectionCallback() {
			
			@Override
			public void onNewSelection(Selection selection) {
				currentSelection = selection;
				if ( isSpeaking )
					say( currentSelection );
			}
			
			@Override
			public void onFail() {
				stop();
				//currentSelection = null;
			}
		});
	}
	
	private void say( Selection selection ) {
		HashMap<String, String> params = new HashMap<String, String>();
		params.put(TTS.KEY_PARAM_UTTERANCE_ID, "cr3UtteranceId");
		mTTS.speak(selection.text, TTS.QUEUE_ADD, params);
	}
	
	private void start() {
		if ( currentSelection==null )
			return;
		isSpeaking = true;
		say( currentSelection );
	}
	
	private boolean isSpeaking; 
	private void stop() {
		isSpeaking = false;
		if ( mTTS.isSpeaking() ) {
			mTTS.stop();
		}
	}
	
	public void pause() {
		if (isSpeaking)
			toggleStartStop();
	}
	
	private void toggleStartStop() {
		if ( isSpeaking ) {
			playPauseButton.setImageResource(R.drawable.ic_media_play);
			stop();
		} else {
			playPauseButton.setImageResource(R.drawable.ic_media_pause);
			start();
		}
	}
	
	@Override
	public void onUtteranceCompleted(String utteranceId) {
		if ( isSpeaking )
			moveSelection( ReaderCommand.DCMD_SELECT_NEXT_SENTENCE );
	}

	public TTSToolbarDlg( CoolReader coolReader, ReaderView readerView, TTS tts )
	{
		mCoolReader = coolReader;
		mReaderView = readerView;
		mAnchor = readerView.getSurface();
		mTTS = tts;
		mTTS.setOnUtteranceCompletedListener(this);

		View panel = (LayoutInflater.from(coolReader.getApplicationContext()).inflate(R.layout.tts_toolbar, null));
		playPauseButton = (ImageButton)panel.findViewById(R.id.tts_play_pause);
		playPauseButton.setImageResource(R.drawable.ic_media_play);
		panel.measure(ViewGroup.LayoutParams.FILL_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
		
		mWindow = new PopupWindow( mAnchor.getContext() );
		mWindow.setBackgroundDrawable(new BitmapDrawable());
		mPanel = panel;
		mPanel.findViewById(R.id.tts_play_pause).setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				toggleStartStop();
			}
		});
		mPanel.findViewById(R.id.tts_back).setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				if ( isSpeaking ) {
					isSpeaking = false;
					mTTS.stop();
					isSpeaking = true;
				}
				moveSelection( ReaderCommand.DCMD_SELECT_PREV_SENTENCE );
			}
		});
		mPanel.findViewById(R.id.tts_forward).setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				if ( isSpeaking ) {
					isSpeaking = false;
					mTTS.stop();
					isSpeaking = true;
				}
				moveSelection( ReaderCommand.DCMD_SELECT_NEXT_SENTENCE );
			}
		});
		mPanel.findViewById(R.id.tts_stop).setOnClickListener(new OnClickListener() {
			public void onClick(View v) {
				stopAndClose();
			}
		});
		mPanel.setFocusable(true);
		mPanel.setEnabled(true);
		mPanel.setOnKeyListener( new OnKeyListener() {

			public boolean onKey(View v, int keyCode, KeyEvent event) {
				if ( event.getAction()==KeyEvent.ACTION_UP ) {
					switch ( keyCode ) {
					case KeyEvent.KEYCODE_VOLUME_DOWN:
					case KeyEvent.KEYCODE_VOLUME_UP:
						return true;
					case KeyEvent.KEYCODE_BACK:
						stopAndClose();
						return true;
					}
				} else if ( event.getAction()==KeyEvent.ACTION_DOWN ) {
					switch ( keyCode ) {
					case KeyEvent.KEYCODE_VOLUME_DOWN: {
						int p = sbVolume.getProgress() - 5;
						if ( p<0 )
							p = 0;
						sbVolume.setProgress(p);
						return true;
					}
					case KeyEvent.KEYCODE_VOLUME_UP:
						int p = sbVolume.getProgress() + 5;
						if ( p>100 )
							p = 100;
						sbVolume.setProgress(p);
						return true;
					}
					if ( keyCode == KeyEvent.KEYCODE_BACK) {
						return true;
					}
				}
				return false;
			}
			
		});

		mWindow.setOnDismissListener(new OnDismissListener() {
			@Override
			public void onDismiss() {
				if ( !closed )
					stopAndClose();
			}
		});
		
		mWindow.setBackgroundDrawable(new BitmapDrawable());
		//mWindow.setAnimationStyle(android.R.style.Animation_Toast);
		//mWindow.setWidth(WindowManager.LayoutParams.WRAP_CONTENT);
		mWindow.setWidth(WindowManager.LayoutParams.FILL_PARENT);
		mWindow.setHeight(WindowManager.LayoutParams.WRAP_CONTENT);
		mWindow.setFocusable(true);
		mWindow.setTouchable(true);
		mWindow.setOutsideTouchable(true);
		mWindow.setContentView(panel);
		
		
		int [] location = new int[2];
		mAnchor.getLocationOnScreen(location);

		mWindow.showAtLocation(mAnchor, Gravity.TOP | Gravity.CENTER_HORIZONTAL, location[0], location[1] + mAnchor.getHeight() - mPanel.getHeight());

		setReaderMode();

		// setup speed && volume seek bars
		sbSpeed = (SeekBar)mPanel.findViewById(R.id.tts_sb_speed);
		sbVolume = (SeekBar)mPanel.findViewById(R.id.tts_sb_volume);
		
		sbSpeed.setMax(100);
		sbSpeed.setProgress(50);
		sbVolume.setMax(100);
		sbVolume.setProgress(mCoolReader.getVolume());
		sbSpeed.setOnSeekBarChangeListener( new OnSeekBarChangeListener() {
			@Override
			public void onProgressChanged(SeekBar seekBar, int progress,
					boolean fromUser) {
				float rate = 1.0f;
				if ( progress<50 )
					rate = 0.3f + 0.7f * progress / 50f;
				else
					rate = 1.0f + 2.5f * (progress-50) / 50f;
				mTTS.setSpeechRate(rate);
			}

			@Override
			public void onStartTrackingTouch(SeekBar seekBar) {
			}

			@Override
			public void onStopTrackingTouch(SeekBar seekBar) {
			}
		});

		sbVolume.setOnSeekBarChangeListener( new OnSeekBarChangeListener() {
			@Override
			public void onProgressChanged(SeekBar seekBar, int progress,
					boolean fromUser) {
				mCoolReader.setVolume(progress);
			}

			@Override
			public void onStartTrackingTouch(SeekBar seekBar) {
			}

			@Override
			public void onStopTrackingTouch(SeekBar seekBar) {
			}
		});
		
		mPanel.requestFocus();
	}
	
}
