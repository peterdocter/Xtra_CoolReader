// CoolReader3 Engine JNI interface
// BASED on Android NDK Plasma example

#include <jni.h>
#include <time.h>
#include <android/log.h>


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "org_coolreader_crengine_Engine.h"
#include "org_coolreader_crengine_DocView.h"

#include "cr3java.h"
#include "../../crengine/include/cr3version.h"
#include "docview.h"
#include "../../crengine/include/crengine.h"
#include "../../crengine/include/epubfmt.h"
#include "../../crengine/include/pdbfmt.h"
#include "../../crengine/include/lvstream.h"


#include <../../crengine/include/fb2def.h>

#define XS_IMPLEMENT_SCHEME 1
#include <../../crengine/include/fb2def.h>
#include <sys/stat.h>

#if defined(__arm__)
#include "coffeecatch/coffeecatch.h"
#include "coffeecatch/coffeejni.h"
#else
#define COFFEE_TRY_JNI(ENV, CODE) CODE;
#endif

#ifdef _DEBUG
// missing in system ZLIB with DEBUG option turned off
int z_verbose=0;
extern "C" void z_error(char * msg);
void z_error(char * msg) {
	fprintf(stderr, "%s\n", msg);
	exit(1);
}
#endif
/// returns current time representation string
static lString16 getDateTimeString( time_t t )
{
    tm * bt = localtime(&t);
    char str[32];
#ifdef _LINUX
    snprintf(str, 32,
#else
    sprintf(str, 
#endif
        "%04d/%02d/%02d %02d:%02d", bt->tm_year+1900, bt->tm_mon+1, bt->tm_mday, bt->tm_hour, bt->tm_min);
    str[31] = 0;
    return Utf8ToUnicode( lString8( str ) );
}

class BookProperties
{
public:
    lString16 filename;
    lString16 title;
    lString16 author;
    lString16 series;
    int filesize;
    lString16 filedate;
    int seriesNumber;
    lString16 language;
};

static bool GetEPUBBookProperties(const char *name, LVStreamRef stream, BookProperties * pBookProps)
{
    LVContainerRef m_arc = LVOpenArchieve( stream );
    if ( m_arc.isNull() )
        return false; // not a ZIP archive

    // check root media type
    lString16 rootfilePath = EpubGetRootFilePath(m_arc);
    if ( rootfilePath.empty() )
    	return false;

    lString16 codeBase;
    codeBase=LVExtractPath(rootfilePath, false);

    LVStreamRef content_stream = m_arc->OpenStream(rootfilePath.c_str(), LVOM_READ);
    if ( content_stream.isNull() )
        return false;

    ldomDocument * doc = LVParseXMLStream( content_stream );
    if ( !doc )
        return false;

    time_t t = (time_t)time(0);
    struct stat fs;
    if ( !stat( name, &fs ) ) {
        t = fs.st_mtime;
    }

    lString16 author = doc->textFromXPath( lString16("package/metadata/creator")).trim();
    lString16 title = doc->textFromXPath( lString16("package/metadata/title")).trim();
    lString16 language = doc->textFromXPath( lString16("package/metadata/language")).trim();

    pBookProps->author = author;
    pBookProps->title = title;
    pBookProps->language = language;

    for ( int i=1; i<20; i++ ) {
        ldomNode * item = doc->nodeFromXPath( lString16("package/metadata/meta[") << fmt::decimal(i) << "]" );
        if ( !item )
            break;
        lString16 name = item->getAttributeValue("name");
        lString16 content = item->getAttributeValue("content");
        if (name == "calibre:series")
        	pBookProps->series = content.trim();
        else if (name == "calibre:series_index")
        	pBookProps->seriesNumber = content.trim().atoi();
    }

    pBookProps->filesize = (long)stream->GetSize();
    pBookProps->filename = lString16(name);
    pBookProps->filedate = getDateTimeString( t );

    delete doc;

    return true;
}

static bool GetBookProperties(const char *name,  BookProperties * pBookProps)
{
    // check archieve
    lString16 arcPathName;
    lString16 arcItemPathName;
    bool isArchiveFile = LVSplitArcName( lString16(name), arcPathName, arcItemPathName );

    // open stream
    LVStreamRef stream = LVOpenFileStream( (isArchiveFile ? arcPathName : Utf8ToUnicode(lString8(name))).c_str() , LVOM_READ);
    if (!stream) {
        return false;
    }

    if ( DetectEpubFormat( stream ) ) {
    	return GetEPUBBookProperties( name, stream, pBookProps );
    }

    time_t t = (time_t)time(0);

    if ( isArchiveFile ) {
        int arcsize = (int)stream->GetSize();
        LVContainerRef container = LVOpenArchieve(stream);
        if ( container.isNull() ) {
            return false;
        }
        stream = container->OpenStream(arcItemPathName.c_str(), LVOM_READ);
        if ( stream.isNull() ) {
            return false;
        }
    }
    struct stat fs;
    if ( !stat( name, &fs ) ) {
        t = fs.st_mtime;
    }

    // read document
#if COMPACT_DOM==1
    ldomDocument doc(stream, 0);
#else
    ldomDocument doc;
#endif
    ldomDocumentWriter writer(&doc, true);
    doc.setNodeTypes( fb2_elem_table );
    doc.setAttributeTypes( fb2_attr_table );
    doc.setNameSpaceTypes( fb2_ns_table );
    LVXMLParser parser( stream, &writer );
    if ( !parser.CheckFormat() ) {
        return false;
    }
    if ( !parser.Parse() ) {
        return false;
    }
    lString16 authors = extractDocAuthors( &doc, lString16("|"), false );
    lString16 title = extractDocTitle( &doc );
    lString16 language = extractDocLanguage( &doc );
    lString16 series = extractDocSeries( &doc, &pBookProps->seriesNumber );
#if SERIES_IN_AUTHORS==1
    if ( !series.empty() )
        authors << "    " << series;
#endif
    pBookProps->title = title;
    pBookProps->author = authors;
    pBookProps->series = series;
    pBookProps->filesize = (long)stream->GetSize();
    pBookProps->filename = lString16(name);
    pBookProps->filedate = getDateTimeString( t );
    pBookProps->language = language;
    return true;
}


/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    scanBookPropertiesInternal
 * Signature: (Lorg/coolreader/crengine/FileInfo;)Z
 */
JNIEXPORT jboolean JNICALL Java_org_coolreader_crengine_Engine_scanBookPropertiesInternal
  (JNIEnv * _env, jclass _engine, jobject _fileInfo)
{
        CRJNIEnv env(_env);
	jclass objclass = env->GetObjectClass(_fileInfo);
	jfieldID fid = env->GetFieldID(objclass, "pathname", "Ljava/lang/String;");
	lString16 filename = env.fromJavaString( (jstring)env->GetObjectField(_fileInfo, fid) );
        fid = env->GetFieldID(objclass, "arcname", "Ljava/lang/String;");
        lString16 arcname = env.fromJavaString( (jstring)env->GetObjectField(_fileInfo, fid) );
	if ( filename.empty() )
		return JNI_FALSE;
	if ( !arcname.empty() )
       filename = arcname + "@/" + filename;

	BookProperties props;
	bool res = GetBookProperties(LCSTR(filename),  &props);
	if ( !res )
		return JNI_FALSE;
	#define SET_STR_FLD(fldname,src) \
	{ \
	    jfieldID fid = env->GetFieldID(objclass, fldname, "Ljava/lang/String;"); \
	    env->SetObjectField(_fileInfo,fid,env.toJavaString(src)); \
	}
	#define SET_INT_FLD(fldname,src) \
	{ \
	    jfieldID fid = env->GetFieldID(objclass, fldname, "I"); \
	    env->SetIntField(_fileInfo,fid,src); \
	}
	SET_STR_FLD("title",props.title);
	SET_STR_FLD("authors",props.author);
	SET_STR_FLD("series",props.series);
	SET_INT_FLD("seriesNumber",props.seriesNumber);
	SET_STR_FLD("language",props.language);
	
	return JNI_TRUE;
}


void drawBookCoverInternal(JNIEnv * _env, jclass _engine, jobject bitmap, jbyteArray _data, jstring _fontFace, jstring _title, jstring _authors, jstring _seriesName, jint seriesNumber, jint bpp)
{
	CRJNIEnv env(_env);
        lString8 fontFace = UnicodeToUtf8(env.fromJavaString(_fontFace));
	lString16 title = env.fromJavaString(_title);
	lString16 authors = env.fromJavaString(_authors);
	lString16 seriesName = env.fromJavaString(_seriesName);
	LVStreamRef stream;
	LVDrawBuf * drawbuf = BitmapAccessorInterface::getInstance()->lock(_env, bitmap);
	if (drawbuf != NULL) {
		LVImageSourceRef image;
		if (_data != NULL && _env->GetArrayLength(_data) > 0) {
			stream = env.jbyteArrayToStream(_data);
			if (!stream.isNull())
				image = LVCreateStreamImageSource(stream);
		}

		int factor = 1;
		int dx = drawbuf->GetWidth();
		int dy = drawbuf->GetHeight();
		int MIN_WIDTH = 300;
		int MIN_HEIGHT = 400;
		if (dx < MIN_WIDTH || dy < MIN_HEIGHT) {
			if (dx * 2 < MIN_WIDTH || dy * 2 < MIN_HEIGHT) {
				dx *= 3;
				dy *= 3;
				factor = 3;
			} else {
				dx *= 2;
				dy *= 2;
				factor = 2;
			}
		}
		LVDrawBuf * drawbuf2 = drawbuf;
		if (factor > 1)
			drawbuf2 = new LVColorDrawBuf(dx, dy, drawbuf->GetBitsPerPixel());

		if (bpp >= 16) {
			// native color resolution
			LVDrawBookCover(*drawbuf2, image, fontFace, title, authors, seriesName, seriesNumber);
			image.Clear();
		} else {
			LVGrayDrawBuf grayBuf(drawbuf2->GetWidth(), drawbuf2->GetHeight(), bpp);
			LVDrawBookCover(grayBuf, image, fontFace, title, authors, seriesName, seriesNumber);
			image.Clear();
			grayBuf.DrawTo(drawbuf2, 0, 0, 0, NULL);
		}

		if (factor > 1) {
			drawbuf->DrawRescaled(drawbuf2, 0, 0, drawbuf->GetWidth(), drawbuf->GetHeight(), 0);
			delete drawbuf2;
		}

		BitmapAccessorInterface::getInstance()->unlock(_env, bitmap, drawbuf);
	}
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    drawBookCoverInternal
 * Signature: (Landroid/graphics/Bitmap;[BLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;II)V
 */
JNIEXPORT void JNICALL Java_org_coolreader_crengine_Engine_drawBookCoverInternal
  (JNIEnv * _env, jclass _engine, jobject bitmap, jbyteArray _data, jstring _fontFace, jstring _title, jstring _authors, jstring _seriesName, jint seriesNumber, jint bpp)
{
	COFFEE_TRY_JNI(_env, drawBookCoverInternal(_env, _engine, bitmap, _data, _fontFace, _title, _authors, _seriesName, seriesNumber, bpp));
}

jbyteArray scanBookCoverInternal
  (JNIEnv * _env, jclass _class, jstring _path)
{
    CRJNIEnv env(_env);
    lString16 path = env.fromJavaString(_path);
    lString16 arcname, item;
    LVStreamRef res;
    jbyteArray array = NULL;
    LVContainerRef arc;
	if (!LVSplitArcName(path, arcname, item)) {
		// not in archive
		LVStreamRef stream = LVOpenFileStream(path.c_str(), LVOM_READ);
		if (!stream.isNull()) {
			arc = LVOpenArchieve(stream);
			if (!arc.isNull()) {
				// ZIP-based format
				if (DetectEpubFormat(stream)) {
					// EPUB
					// extract coverpage from epub
					res = GetEpubCoverpage(arc);
				}
			} else {
				res = GetFB2Coverpage(stream);
				if (res.isNull()) {
					doc_format_t fmt;
					if (DetectPDBFormat(stream, fmt)) {
						res = GetPDBCoverpage(stream);
					}
				}
			}
		}
	} else {
		LVStreamRef arcstream = LVOpenFileStream(arcname.c_str(), LVOM_READ);
		if (!arcstream.isNull()) {
			arc = LVOpenArchieve(arcstream);
			if (!arc.isNull()) {
				LVStreamRef stream = arc->OpenStream(item.c_str(), LVOM_READ);
				if (!stream.isNull()) {
					res = GetFB2Coverpage(stream);
					if (res.isNull()) {
						doc_format_t fmt;
						if (DetectPDBFormat(stream, fmt)) {
							res = GetPDBCoverpage(stream);
						}
					}
				}
			}
		}
	}
	if (!res.isNull())
		array = env.streamToJByteArray(res);
    return array;
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    scanBookCoverInternal
 * Signature: (Ljava/lang/String;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_coolreader_crengine_Engine_scanBookCoverInternal
  (JNIEnv * _env, jclass _class, jstring _path)
{
	jbyteArray res = NULL;
	COFFEE_TRY_JNI(_env, res = scanBookCoverInternal( _env, _class, _path));
	return res;
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    getArchiveItemsInternal
 * Signature: (Ljava/lang/String;)[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_org_coolreader_crengine_Engine_getArchiveItemsInternal
  (JNIEnv * _env, jclass, jstring jarcName)
{
    CRJNIEnv env(_env);
    lString16 arcName = env.fromJavaString(jarcName);
    lString16Collection list;
    
    LVStreamRef stream = LVOpenFileStream( arcName.c_str(), LVOM_READ );
    if ( !stream.isNull() ) {
        LVContainerRef arc = LVOpenArchieve(stream);
        if ( !arc.isNull() ) {
            // convert
            for ( int i=0; i<arc->GetObjectCount(); i++ ) {
                const LVContainerItemInfo * item = arc->GetObjectInfo(i);
                if ( item->IsContainer())
                    continue;
                list.add( item->GetName() );
                list.add( lString16::itoa(item->GetSize()) );
            }
        }
    }
    return env.toJavaStringArray(list);
}


/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    setHyphenationMethod
 * Signature: (I[B)Z
 */
JNIEXPORT jboolean JNICALL Java_org_coolreader_crengine_Engine_setHyphenationMethod
  (JNIEnv * _env, jclass _engine, jint method, jbyteArray data)
{
	CRJNIEnv env(_env);
        if ( method==0 ) {
		return HyphMan::activateDictionary(lString16(HYPH_DICT_ID_NONE));
	} else if ( method==1 ) {
		return HyphMan::activateDictionary(lString16(HYPH_DICT_ID_ALGORITHM));
	} else {
		LVStreamRef stream = env.jbyteArrayToStream( data );
		bool res = HyphMan::activateDictionaryFromStream(stream);
		if ( !res ) {
			HyphMan::activateDictionary(lString16(HYPH_DICT_ID_NONE));
			return false;
		}
		return true;
	}
}


class JNICDRLogger : public CRLog
{
public:
    JNICDRLogger()
    {
    	curr_level = CRLog::LL_DEBUG;
    }
protected:
  
	virtual void log( const char * lvl, const char * msg, va_list args)
	{
	    #define MAX_LOG_MSG_SIZE 1024
		static char buffer[MAX_LOG_MSG_SIZE+1];
		vsnprintf(buffer, MAX_LOG_MSG_SIZE, msg, args);
		int level = ANDROID_LOG_DEBUG;
		if ( !strcmp(lvl, "FATAL") )
			level = ANDROID_LOG_FATAL;
		else if ( !strcmp(lvl, "ERROR") )
			level = ANDROID_LOG_ERROR;
		else if ( !strcmp(lvl, "WARN") )
			level = ANDROID_LOG_WARN;
		else if ( !strcmp(lvl, "INFO") )
			level = ANDROID_LOG_INFO;
		else if ( !strcmp(lvl, "DEBUG") )
			level = ANDROID_LOG_DEBUG;
		else if ( !strcmp(lvl, "TRACE") )
			level = ANDROID_LOG_VERBOSE;
		__android_log_write(level, LOG_TAG, buffer);
	}
};

void cr3androidFatalErrorHandler(int errorCode, const char * errorText )
{
	LOGE("CoolReader Fatal Error #%d: %s", errorCode, errorText);
	LOGASSERTFAILED("CoolReader Fatal Error", "CoolReader Fatal Error #%d: %s", errorCode, errorText);
}

/// set fatal error handler
void crSetFatalErrorHandler( lv_FatalErrorHandler_t * handler );

jboolean initInternal(JNIEnv * penv, jclass obj, jobjectArray fontArray) {

	CRJNIEnv env(penv);

	// to catch crashes and remove current cache file on crash (SIGSEGV etc.)
	crSetSignalHandler();

	// set fatal error handler
	crSetFatalErrorHandler( &cr3androidFatalErrorHandler );
        HyphMan::initDictionaries(lString16::empty_str); //don't look for dictionaries
	HyphMan::activateDictionary(lString16(HYPH_DICT_ID_NONE));
        InitFontManager(lString8::empty_str);
	lString16Collection fonts;
	env.fromJavaStringArray(fontArray, fonts);
	int len = fonts.length();
	for ( int i=0; i<len; i++ ) {
		lString8 fontName = UnicodeToUtf8(fonts[i]);
		fontMan->RegisterFont( fontName );                
	}
	return fontMan->GetFontCount() ? JNI_TRUE : JNI_FALSE;
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    initInternal
 * Signature: ([Ljava/lang/String;)Z
 */
JNIEXPORT jboolean JNICALL Java_org_coolreader_crengine_Engine_initInternal
  (JNIEnv * penv, jclass obj, jobjectArray fontArray)
{
	jboolean res = JNI_FALSE;
	COFFEE_TRY_JNI(penv, res = initInternal(penv, obj, fontArray));
	return res;
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    uninitInternal
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_coolreader_crengine_Engine_uninitInternal
  (JNIEnv *, jclass)
{
	HyphMan::uninit();
	ShutdownFontManager();
	CRLog::setLogger(NULL);
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    getFontFaceListInternal
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_org_coolreader_crengine_Engine_getFontFaceListInternal
  (JNIEnv * penv, jclass obj)
{
	CRJNIEnv env(penv);
	lString16Collection list;
	COFFEE_TRY_JNI(penv, fontMan->getFaceList(list));
	return env.toJavaStringArray(list);
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    setCacheDirectoryInternal
 * Signature: (Ljava/lang/String;I)Z
 */
JNIEXPORT jboolean JNICALL Java_org_coolreader_crengine_Engine_setCacheDirectoryInternal
  (JNIEnv * penv, jclass obj, jstring dir, jint size)
{
	CRJNIEnv env(penv);
	bool res = false;
	COFFEE_TRY_JNI(penv, res = ldomDocCache::init(env.fromJavaString(dir), size ));
	return res ? JNI_TRUE : JNI_FALSE;
}

/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    isLink
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_coolreader_crengine_Engine_isLink
  (JNIEnv * env, jclass obj, jstring pathname)
{
	if (!pathname)
		return NULL;
	int res = JNI_FALSE;
	jboolean iscopy;
	const char * s = env->GetStringUTFChars(pathname, &iscopy);
	struct stat st;
	lString8 path;
	if ( !lstat( s, &st) ) {
		if ( S_ISLNK(st.st_mode) ) {
			char buf[2048];
			int len = readlink(s, buf, sizeof(buf) - 1);
			if (len != -1) {
				buf[len] = 0;
				path = lString8(buf);
			}
		}
	}
	env->ReleaseStringUTFChars(pathname, s);
	return !path.empty() ? (jstring)env->NewGlobalRef(env->NewStringUTF(path.c_str())) : NULL;
}


/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    suspendLongOperationInternal
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_coolreader_crengine_Engine_suspendLongOperationInternal
  (JNIEnv *, jclass)
{
	_timeoutControl.cancel();
}


#define BUTTON_BACKLIGHT_CONTROL_PATH "/sys/class/leds/button-backlight/brightness"
/*
 * Class:     org_coolreader_crengine_Engine
 * Method:    setKeyBacklightInternal
 * Signature: (I)Z
 */
JNIEXPORT jboolean JNICALL Java_org_coolreader_crengine_Engine_setKeyBacklightInternal
  (JNIEnv *, jclass, jint n)
{
	FILE * f = fopen(BUTTON_BACKLIGHT_CONTROL_PATH, "wb");
	if (!f)
		return JNI_FALSE;
	fwrite(n ? "1" : "0", 1, 1, f);
	fclose(f);
	return JNI_TRUE;
}

//=====================================================================

static JNINativeMethod sEngineMethods[] = {
  /* name, signature, funcPtr */
  {"initInternal", "([Ljava/lang/String;)Z", (void*)Java_org_coolreader_crengine_Engine_initInternal},
  {"uninitInternal", "()V", (void*)Java_org_coolreader_crengine_Engine_uninitInternal},
  {"getFontFaceListInternal", "()[Ljava/lang/String;", (void*)Java_org_coolreader_crengine_Engine_getFontFaceListInternal},
  {"setCacheDirectoryInternal", "(Ljava/lang/String;I)Z", (void*)Java_org_coolreader_crengine_Engine_setCacheDirectoryInternal},
  {"scanBookPropertiesInternal", "(Lorg/coolreader/crengine/FileInfo;)Z", (void*)Java_org_coolreader_crengine_Engine_scanBookPropertiesInternal},
  {"setHyphenationMethod", "(I[B)Z", (void*)Java_org_coolreader_crengine_Engine_setHyphenationMethod},
  {"getArchiveItemsInternal", "(Ljava/lang/String;)[Ljava/lang/String;", (void*)Java_org_coolreader_crengine_Engine_getArchiveItemsInternal},
  {"isLink", "(Ljava/lang/String;)Ljava/lang/String;", (void*)Java_org_coolreader_crengine_Engine_isLink},
  {"suspendLongOperationInternal", "()V", (void*)Java_org_coolreader_crengine_Engine_suspendLongOperationInternal},
  {"setKeyBacklightInternal", "(I)Z", (void*)Java_org_coolreader_crengine_Engine_setKeyBacklightInternal},
  {"scanBookCoverInternal", "(Ljava/lang/String;)[B", (void*)Java_org_coolreader_crengine_Engine_scanBookCoverInternal},
  {"drawBookCoverInternal", "(Landroid/graphics/Bitmap;[BLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;II)V", (void*)Java_org_coolreader_crengine_Engine_drawBookCoverInternal},
};


static JNINativeMethod sDocViewMethods[] = {
  /* name, signature, funcPtr */
  {"createInternal", "()V", (void*)Java_org_coolreader_crengine_DocView_createInternal},
  {"destroyInternal", "()V", (void*)Java_org_coolreader_crengine_DocView_destroyInternal},
  {"getPageImageInternal", "(Landroid/graphics/Bitmap;I)V", (void*)Java_org_coolreader_crengine_DocView_getPageImageInternal},
  {"loadDocumentInternal", "(Ljava/lang/String;)Z", (void*)Java_org_coolreader_crengine_DocView_loadDocumentInternal},
  {"getSettingsInternal", "()Ljava/util/Properties;", (void*)Java_org_coolreader_crengine_DocView_getSettingsInternal},
  {"applySettingsInternal", "(Ljava/util/Properties;)Z", (void*)Java_org_coolreader_crengine_DocView_applySettingsInternal},
  {"setStylesheetInternal", "(Ljava/lang/String;)V", (void*)Java_org_coolreader_crengine_DocView_setStylesheetInternal},
  {"resizeInternal", "(II)V", (void*)Java_org_coolreader_crengine_DocView_resizeInternal},
  {"doCommandInternal", "(II)Z", (void*)Java_org_coolreader_crengine_DocView_doCommandInternal},
  {"getCurrentPageBookmarkInternal", "()Lorg/coolreader/crengine/Bookmark;", (void*)Java_org_coolreader_crengine_DocView_getCurrentPageBookmarkInternal},
  {"goToPositionInternal", "(Ljava/lang/String;Z)Z", (void*)Java_org_coolreader_crengine_DocView_goToPositionInternal},
  {"getPositionPropsInternal", "(Ljava/lang/String;)Lorg/coolreader/crengine/PositionProperties;", (void*)Java_org_coolreader_crengine_DocView_getPositionPropsInternal},
  {"updateBookInfoInternal", "(Lorg/coolreader/crengine/BookInfo;)V", (void*)Java_org_coolreader_crengine_DocView_updateBookInfoInternal},
  {"getTOCInternal", "()Lorg/coolreader/crengine/TOCItem;", (void*)Java_org_coolreader_crengine_DocView_getTOCInternal},
  {"clearSelectionInternal", "()V", (void*)Java_org_coolreader_crengine_DocView_clearSelectionInternal},
  {"findTextInternal", "(Ljava/lang/String;III)Z", (void*)Java_org_coolreader_crengine_DocView_findTextInternal},
  {"setBatteryStateInternal", "(I)V", (void*)Java_org_coolreader_crengine_DocView_setBatteryStateInternal},
  {"getCoverPageDataInternal", "()[B", (void*)Java_org_coolreader_crengine_DocView_getCoverPageDataInternal},
  {"setPageBackgroundTextureInternal", "([BI)V", (void*)Java_org_coolreader_crengine_DocView_setPageBackgroundTextureInternal},
  {"updateSelectionInternal", "(Lorg/coolreader/crengine/Selection;)V", (void*)Java_org_coolreader_crengine_DocView_updateSelectionInternal},
  {"checkLinkInternal", "(III)Ljava/lang/String;", (void*)Java_org_coolreader_crengine_DocView_checkLinkInternal},
  {"goLinkInternal", "(Ljava/lang/String;)I", (void*)Java_org_coolreader_crengine_DocView_goLinkInternal},
  {"moveSelectionInternal", "(Lorg/coolreader/crengine/Selection;II)Z", (void*)Java_org_coolreader_crengine_DocView_moveSelectionInternal},
  {"swapToCacheInternal", "()I", (void*)Java_org_coolreader_crengine_DocView_swapToCacheInternal},
  {"checkImageInternal", "(IILorg/coolreader/crengine/ImageInfo;)Z", (void*)Java_org_coolreader_crengine_DocView_checkImageInternal},
  {"drawImageInternal", "(Landroid/graphics/Bitmap;ILorg/coolreader/crengine/ImageInfo;)Z", (void*)Java_org_coolreader_crengine_DocView_drawImageInternal},
  {"closeImageInternal", "()Z", (void*)Java_org_coolreader_crengine_DocView_closeImageInternal},
  {"hilightBookmarksInternal", "([Lorg/coolreader/crengine/Bookmark;)V", (void*)Java_org_coolreader_crengine_DocView_hilightBookmarksInternal},
  {"checkBookmarkInternal", "(IILorg/coolreader/crengine/Bookmark;)Z", (void*)Java_org_coolreader_crengine_DocView_checkBookmarkInternal},
  {"isRenderedInternal", "()Z", (void*)Java_org_coolreader_crengine_DocView_isRenderedInternal}
};

/*
 * Register native JNI-callable methods.
 *
 * "className" looks like "java/lang/String".
 */
static int jniRegisterNativeMethods(JNIEnv* env, const char* className,
    const JNINativeMethod* gMethods, int numMethods)
{
    jclass clazz;

    clazz = env->FindClass(className);
    if (clazz == NULL) {
        LOGE("Native registration unable to find class '%s'\n", className);
        return -1;
    }
    if (env->RegisterNatives(clazz, gMethods, numMethods) < 0) {
        LOGE("RegisterNatives failed for '%s'\n", className);
        return -1;
    }
    return 0;
}


jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
   JNIEnv* env = NULL;
   jint res = -1;
 
#ifdef JNI_VERSION_1_6
    if (res==-1 && vm->GetEnv((void**) &env, JNI_VERSION_1_6) == JNI_OK) {
   	    res = JNI_VERSION_1_6;
    }
#endif
#ifdef JNI_VERSION_1_4
    if (res==-1 && vm->GetEnv((void**) &env, JNI_VERSION_1_4) == JNI_OK) {
   	    res = JNI_VERSION_1_4;
    }
#endif
#ifdef JNI_VERSION_1_2
    if (res==-1 && vm->GetEnv((void**) &env, JNI_VERSION_1_2) == JNI_OK) {
   	    res = JNI_VERSION_1_2;
    }
#endif
	if ( res==-1 )
		return res;
 
    jniRegisterNativeMethods(env, "org/coolreader/crengine/Engine", sEngineMethods, sizeof(sEngineMethods)/sizeof(JNINativeMethod));
    jniRegisterNativeMethods(env, "org/coolreader/crengine/DocView", sDocViewMethods, sizeof(sDocViewMethods)/sizeof(JNINativeMethod));
    return res;
}
