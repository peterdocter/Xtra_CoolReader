package org.coolreader.crengine;

import java.util.ArrayList;
import java.util.concurrent.Callable;

import org.coolreader.crengine.ReaderView.Sync;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;

/**
 * Allows running tasks either in background thread or in GUI thread.
 */
public class BackgroundThread extends Thread {
	
	private final static Object LOCK = new Object(); 

	private static BackgroundThread instance;
	
	// singleton
	public static BackgroundThread instance()
	{
		if ( instance==null ) {
			synchronized( LOCK ) {
				if ( instance==null ) {
					instance = new BackgroundThread();
					instance.start();
				}
			}
		}
		return instance;
	}
	
	public static Handler getBackgroundHandler() {
		if (instance == null)
			return null;
		return instance.handler;
	}

	public static Handler getGUIHandler() {
		if (instance().guiHandler == null)
			return null;
		return instance().guiHandler;
	}

	public final static boolean CHECK_THREAD_CONTEXT = true; 

	/**
	 * Throws exception if not in background thread.
	 */
	public final static void ensureBackground()
	{
		if ( CHECK_THREAD_CONTEXT && !isBackgroundThread() ) {
			L.e("not in background thread", new Exception("ensureInBackgroundThread() is failed"));
			throw new RuntimeException("ensureInBackgroundThread() is failed");
		}
	}
	
	/**
	 * Throws exception if not in GUI thread.
	 */
	public final static void ensureGUI()
	{
		if ( CHECK_THREAD_CONTEXT && isBackgroundThread() ) {
			L.e("not in GUI thread", new Exception("ensureGUI() is failed"));
			throw new RuntimeException("ensureGUI() is failed");
		}
	}
	
	// 
	private Handler handler;
	private ArrayList<Runnable> posted = new ArrayList<Runnable>();
	private Handler guiHandler;
	private ArrayList<Runnable> postedGUI = new ArrayList<Runnable>();

	/**
	 * Set view to post GUI tasks to.
	 * @param guiTarget is view to post GUI tasks to.
	 */
	public void setGUIHandler(Handler guiHandler) {
		this.guiHandler = guiHandler;
		if (guiHandler != null) {
			// forward already posted events
			synchronized(postedGUI) {
				for ( Runnable task : postedGUI )
					guiHandler.post( task );
			}
		}
	}

	/**
	 * Create background thread executor.
	 */
	private BackgroundThread() {
		super();
		setName("BackgroundThread" + Integer.toHexString(hashCode()));
	}

	@Override
	public void run() {
		Looper.prepare();
		handler = new Handler() {
			public void handleMessage( Message message )
			{
			}
		};
		synchronized(posted) {
			for ( Runnable task : posted ) {
				handler.post(task);
			}
			posted.clear();
		}
		Looper.loop();
		handler = null;
		instance = null;
	}

	private Runnable guard( final Runnable r )
	{
		return r;
	}

	/**
	 * Post runnable to be executed in background thread.
	 * @param task is runnable to execute in background thread.
	 */
	public void postBackground( Runnable task )
	{
		Engine.suspendLongOperation();
		if ( mStopped ) {
			postGUI( task );
			return;
		}
		task = guard(task);
		if ( handler==null ) {
			synchronized(posted) {
				posted.add(task);
			}
		} else {
			handler.post(task);
		}
	}

	/**
	 * Post runnable to be executed in GUI thread
	 * @param task is runnable to execute in GUI thread
	 */
	public void postGUI( Runnable task )
	{
		postGUI(task, 0);
	}

	static int delayedTaskId = 0;
	/**
	 * Post runnable to be executed in GUI thread
	 * @param task is runnable to execute in GUI thread
	 * @param delay is delay before running task, in millis
	 */
	public void postGUI(final Runnable task, final long delay)
	{
		if ( guiHandler==null ) {
			synchronized( postedGUI ) {
				postedGUI.add(task);
			}
		} else {
			if ( delay>0 ) {
				final int id = ++delayedTaskId;
				guiHandler.postDelayed(new Runnable() {
					@Override
					public void run() {
						task.run();
					}
				}, delay);
			} else
				guiHandler.post(task);
		}
	}

	/**
	 * Run task instantly if called from the same thread, or post it through message queue otherwise.
	 * @param task is task to execute
	 */
	public void executeBackground( Runnable task )
	{
		Engine.suspendLongOperation();
		task = guard(task);
		if (isBackgroundThread() || mStopped)
			task.run(); // run in this thread
		else 
			postBackground(task); // post
	}

	// assume there are only two threads: main GUI and background
	public static boolean isGUIThread()
	{
		return !isBackgroundThread();
	}

	public static boolean isBackgroundThread()
	{
		return (Thread.currentThread() == instance);
	}

	public void executeGUI( Runnable task )
	{
		if (isGUIThread())
			task.run(); // run in this thread
		else
			postGUI(task);
	}

    public <T> Callable<T> guard( final Callable<T> task )
    {
    	return new Callable<T>() {
    		public T call() throws Exception {
    			return task.call();
    		}
    	};
    }
    
    /**
     * Waits until all pending background tasks are executed.
     */
    public void syncWithBackground() {
    	callBackground( new Callable<Integer>() {
			@Override
			public Integer call() throws Exception {
				return null;
			}
    	});
    }
	
    public <T> T callBackground( final Callable<T> srcTask )
    {
    	final Callable<T> task = srcTask; //guard(srcTask);
    	if ( isBackgroundThread() ) {
    		try {
    			return task.call();
    		} catch ( Exception e ) {
    			return null;
    		}
    	}
    	final Sync<T> sync = new Sync<T>();
    	postBackground( new Runnable() {
    		public void run() {
    			try {
    				sync.set( task.call() );
    			} catch ( Exception e ) {
    				sync.set( null );
    			}
    		}
    	});
    	T res = sync.get();
    	return res;
    }
	
    private final static boolean DBG = false; 
    
    public <T> T callGUI( final Callable<T> task )
    {
    	if ( isGUIThread() ) {
    		try {
    			return task.call();
    		} catch ( Exception e ) {
    			return null;
    		}
    	}
    	final Sync<T> sync = new Sync<T>();
    	postGUI( new Runnable() {
    		public void run() {
    	    	T result = null;
    			try {
                        result = task.call();
    			} catch ( Exception e ) {
    				throw new RuntimeException(e);
    			}
    			try {
    				sync.set( result );
    			} catch ( Exception e ) {
    				throw new RuntimeException(e);
    			}
    		}
    	});
    	T res = sync.get();
    	return res;
    }
	
	private boolean mStopped = false;
	
	public void waitForBackgroundCompletion() {
		Engine.suspendLongOperation();
		callBackground(new Callable<Object>() {
			public Object call() {
				return null;
			}
		});
	}
	
	public void quit() {
		postBackground(new Runnable() {
			@Override
			public void run() {
				if (handler != null) {
					handler.getLooper().quit();
				}
			}
		});
	}
}
