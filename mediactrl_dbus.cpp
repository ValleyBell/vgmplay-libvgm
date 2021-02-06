/*
DBus/MPRIS for VGMPlay.
By Tasos Sahanidis <vgmsrc@tasossah.com>

required packages:
libdbus-1-dev

They weren't lying when they said that using libdbus directly signs you up for some pain...
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <dbus/dbus.h>

#include <stdtype.h>
#include <player/playera.hpp>
#include "utils.hpp"
#include "mediainfo.hpp"
#include "mediactrl.hpp"

// DBus MPRIS Constants
#define DBUS_MPRIS_PATH             "/org/mpris/MediaPlayer2"
#define DBUS_MPRIS_MEDIAPLAYER2     "org.mpris.MediaPlayer2"
#define DBUS_MPRIS_PLAYER           DBUS_MPRIS_MEDIAPLAYER2 ".Player"
#define DBUS_MPRIS_VGMPLAY          DBUS_MPRIS_MEDIAPLAYER2 ".vgmplay"
#define DBUS_PROPERTIES             "org.freedesktop.DBus.Properties"

//#define DBUS_DEBUG

// MPRIS Metadata Struct
typedef struct DBusMetadata_
{
	const void* title;
	const char* dbusType;
	void* content;
	int contentType;
	size_t childLen;
} DBusMetadata;


// Defines for the DBUS Signal handler
#define SIGNAL_METADATA    0x01 // Metadata changed
#define SIGNAL_PLAYSTATUS  0x02 // Playback Status Changed
#define SIGNAL_SEEKSTATUS  0x04 // Seek Status Changed
#define SIGNAL_SEEK        0x08 // Seeked
#define SIGNAL_CONTROLS    0x10 // Playback controls need to be updated (CanGoNext/Previous)
#define SIGNAL_VOLUME      0x20 // Volume needs to be updated
#define SIGNAL_ALL         0xFF // All Signals

static DBusConnection* connection = NULL;

static MediaInfo* mInf = NULL;

// Misc Helper Functions

// Return current position in samples
static inline INT32 USec2Samples(dbus_int64_t UsecPos, const PlayerA& player)
{
	return (INT32)((UsecPos / 1.0E+6) * (double)player.GetSampleRate());
}

static inline dbus_int64_t Time2USec(double time)
{
	return (dbus_int64_t)(time * 1.0E+6);
}

static std::string Path2FileURL(const std::string& path)
{
	if (path.empty())
		return path;
	return std::string("file://") + urlencode(GetAbsolutePath(path));
}

// DBus Helper Functions
static void DBusEmptyMethodResponse(DBusConnection* connection, DBusMessage* request)
{
	DBusMessage* reply = dbus_message_new_method_return(request);
	dbus_message_append_args(reply, DBUS_TYPE_INVALID);
	dbus_connection_send(connection, reply, NULL);
	dbus_message_unref(reply);
}

static void DBusReplyToIntrospect(DBusConnection* connection, DBusMessage* request)
{
	static const char* introspection_data =
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
"<node>\n"
"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
"    <method name=\"Introspect\">\n"
"      <arg name=\"data\" direction=\"out\" type=\"s\"/>\n"
"    </method>\n"
"  </interface>\n"
"  <interface name=\"org.freedesktop.DBus.Peer\">\n"
"    <method name=\"Ping\"/>\n"
"    <method name=\"GetMachineId\">\n"
"      <arg type=\"s\" name=\"machine_uuid\" direction=\"out\"/>\n"
"    </method>\n"
"  </interface>\n"
"  <interface name=\"org.freedesktop.DBus.Properties\">\n"
"    <method name=\"Get\">\n"
"      <arg direction=\"in\" type=\"s\"/>\n"
"      <arg direction=\"in\" type=\"s\"/>\n"
"      <arg direction=\"out\" type=\"v\"/>\n"
"    </method>\n"
"    <method name=\"Set\">\n"
"      <arg direction=\"in\" type=\"s\"/>\n"
"      <arg direction=\"in\" type=\"s\"/>\n"
"      <arg direction=\"in\" type=\"v\"/>\n"
"    </method>\n"
"    <method name=\"GetAll\">\n"
"      <arg direction=\"in\" type=\"s\"/>\n"
"      <arg direction=\"out\" type=\"a{sv}\"/>\n"
"    </method>\n"
"    <signal name=\"PropertiesChanged\">\n"
"      <arg type=\"s\"/>\n"
"      <arg type=\"a{sv}\"/>\n"
"      <arg type=\"as\"/>\n"
"    </signal>\n"
"  </interface>\n"
"  <interface name=\"" DBUS_MPRIS_MEDIAPLAYER2 "\">\n"
"    <property name=\"Identity\" type=\"s\" access=\"read\" />\n"
"    <property name=\"DesktopEntry\" type=\"s\" access=\"read\" />\n"
"    <property name=\"SupportedMimeTypes\" type=\"as\" access=\"read\" />\n"
"    <property name=\"SupportedUriSchemes\" type=\"as\" access=\"read\" />\n"
"    <property name=\"HasTrackList\" type=\"b\" access=\"read\" />\n"
"    <property name=\"CanQuit\" type=\"b\" access=\"read\" />\n"
"    <property name=\"CanRaise\" type=\"b\" access=\"read\" />\n"
"    <method name=\"Quit\" />\n"
"    <method name=\"Raise\" />\n"
"  </interface>\n"
"  <interface name=\"" DBUS_MPRIS_PLAYER "\">\n"
"    <property name=\"Metadata\" type=\"a{sv}\" access=\"read\" />\n"
"    <property name=\"PlaybackStatus\" type=\"s\" access=\"read\" />\n"
"    <property name=\"Volume\" type=\"d\" access=\"readwrite\" />\n"
"    <property name=\"Position\" type=\"x\" access=\"read\" />\n"
"    <property name=\"Rate\" type=\"d\" access=\"readwrite\" />\n"
"    <property name=\"MinimumRate\" type=\"d\" access=\"readwrite\" />\n"
"    <property name=\"MaximumRate\" type=\"d\" access=\"readwrite\" />\n"
"    <property name=\"CanControl\" type=\"b\" access=\"read\" />\n"
"    <property name=\"CanGoNext\" type=\"b\" access=\"read\" />\n"
"    <property name=\"CanGoPrevious\" type=\"b\" access=\"read\" />\n"
"    <property name=\"CanPlay\" type=\"b\" access=\"read\" />\n"
"    <property name=\"CanPause\" type=\"b\" access=\"read\" />\n"
"    <property name=\"CanSeek\" type=\"b\" access=\"read\" />\n"
"    <method name=\"Previous\" />\n"
"    <method name=\"Next\" />\n"
"    <method name=\"Stop\" />\n"
"    <method name=\"Play\" />\n"
"    <method name=\"Pause\" />\n"
"    <method name=\"PlayPause\" />\n"
"    <method name=\"Seek\">\n"
"      <arg type=\"x\" direction=\"in\" />\n"
"    </method>"
"    <method name=\"OpenUri\">\n"
"      <arg type=\"s\" direction=\"in\" />\n"
"    </method>\n"
"    <method name=\"SetPosition\">\n"
"      <arg type=\"o\" direction=\"in\" />\n"
"      <arg type=\"x\" direction=\"in\" />\n"
"    </method>\n"
"    <signal name=\"Seeked\">\n"
"      <arg type=\"x\" name=\"Position\"/>\n"
"    </signal>\n"
"  </interface>\n"
"</node>\n"
;

	DBusMessage* reply = dbus_message_new_method_return(request);
	dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_data, DBUS_TYPE_INVALID);
	dbus_connection_send(connection, reply, NULL);
	dbus_message_unref(reply);
}

static void DBusReplyWithVariant(DBusMessageIter* args, int type, const char* type_as_string, void* response)
{
	DBusMessageIter subargs;
	dbus_message_iter_open_container(args, DBUS_TYPE_VARIANT, type_as_string, &subargs);
		dbus_message_iter_append_basic(&subargs, type, response);
	dbus_message_iter_close_container(args, &subargs);
}

void DBusAppendCanGoNext(DBusMessageIter* args)
{
	dbus_bool_t response = (mInf->_pbSongID + 1 < mInf->_pbSongCnt) ? TRUE : FALSE;
	DBusReplyWithVariant(args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
}

void DBusAppendCanGoPrevious(DBusMessageIter* args)
{
	dbus_bool_t response = (mInf->_pbSongID > 0) ? TRUE : FALSE;
	DBusReplyWithVariant(args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
}

static void DBusSendMetadataArray(DBusMessageIter* dict_root, DBusMetadata meta[], size_t dbus_meta_array_len)
{
	DBusMessageIter root_variant, dict, dict_entry, variant;

	dbus_message_iter_open_container(dict_root, DBUS_TYPE_VARIANT, "a{sv}", &root_variant);
		// Open Root Container
		dbus_message_iter_open_container(&root_variant, DBUS_TYPE_ARRAY, "{sv}", &dict);
			for(size_t i = 0; i < dbus_meta_array_len; i++)
			{
				DBusMetadata& md = meta[i];
				// Ignore empty strings
				if(md.contentType == DBUS_TYPE_STRING)
				{
					char** content = (char**)md.content;
					if(*content == NULL || (*content)[0] == '\0')
						continue;
				}
				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &md.title);
					// Field Value
					dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, md.dbusType, &variant);

					if(md.contentType == DBUS_TYPE_ARRAY)
					{
						DBusMetadata* content = (DBusMetadata*)md.content;
						DBusMessageIter array_root;

						dbus_message_iter_open_container(&variant, md.contentType, md.dbusType, &array_root);

							for(size_t len = 0; len < md.childLen; len++)
							{
								dbus_message_iter_append_basic(&array_root, content[len].contentType, content[len].content);
							}

						dbus_message_iter_close_container(&variant, &array_root);
					}
					else
					{
						dbus_message_iter_append_basic(&variant, md.contentType, md.content);
					}

					dbus_message_iter_close_container(&dict_entry, &variant);

				dbus_message_iter_close_container(&dict, &dict_entry);
			}
		// Close Root Container
		dbus_message_iter_close_container(&root_variant, &dict);
	dbus_message_iter_close_container(dict_root, &root_variant);
}

static void DBusSendMetadata(DBusMessageIter* dict_root)
{
	// Send an empty array in a variant if nothing is playing
	if(!(mInf->_playState & PLAYSTATE_PLAY))
	{
		DBusSendMetadataArray(dict_root, NULL, 0);
		return;
	}

	// Prepare metadata
	const char* utf8album = mInf->GetSongTagForDisp("GAME"); // Album
	const char* utf8title = mInf->GetSongTagForDisp("TITLE"); // Title
	dbus_int64_t songlen = Time2USec(mInf->_player.GetTotalTime(1)); // Length
	dbus_int64_t looplen = Time2USec(mInf->_player.GetLoopTime()); // Loop point
	dbus_uint32_t version = mInf->_fileVerNum; // VGM File version
	const char* utf8artist = mInf->GetSongTagForDisp("ARTIST"); // Artist
	const char* utf8release = mInf->GetSongTagForDisp("DATE"); // Game release date
	const char* utf8creator = mInf->GetSongTagForDisp("ENCODED_BY"); // VGM File Creator
	const char* utf8notes = mInf->GetSongTagForDisp("COMMENT"); // Notes
	const char* utf8system = mInf->GetSongTagForDisp("SYSTEM"); // System

	// Track Number in playlist
	dbus_int32_t tracknum = 0;
	if(mInf->_playlistTrkID != (size_t)-1)
		tracknum = (dbus_int32_t)(1 + mInf->_playlistTrkID);

	// URL encoded file path
	std::string songURL = Path2FileURL(mInf->_songPath);
	const char* songurl = songURL.c_str();

	// URL encode the path to the png
	std::string artURL = Path2FileURL(mInf->_albumImgPath);
	const char* arturl = artURL.c_str();

	// Encapsulate some data in DBusMetadata Arrays
	// Artist Array
	// TODO: split artists at , and ; and &
	DBusMetadata dbusartist[1] =
	{
		{ "", DBUS_TYPE_STRING_AS_STRING, &utf8artist, DBUS_TYPE_STRING, 0 },
	};

	// Genre Array
	const char* genre = "Video Game Music";
	DBusMetadata dbusgenre[1] =
	{
		{ "", DBUS_TYPE_STRING_AS_STRING, &genre, DBUS_TYPE_STRING, 0 },
	};

	// Generate chips array
	std::vector<DBusMetadata> chips;
	std::vector<const char*> chipPtrs;
	for (size_t curDev = 0; curDev < mInf->_chipList.size(); curDev ++)
	{
		const MediaInfo::DeviceItem& di = mInf->_chipList[curDev];
		chipPtrs.push_back(di.name.c_str());
	}
	for (size_t curDev = 0; curDev < chipPtrs.size(); curDev ++)
	{
		DBusMetadata cm = {
			NULL, DBUS_TYPE_STRING_AS_STRING,
			(void*)&chipPtrs[curDev], DBUS_TYPE_STRING,
			0
		};
		chips.push_back(cm);
	}

	// Stubs
	const char* trackid = DBUS_MPRIS_PATH "/CurrentTrack";
	//const char* lastused = "2018-01-04T12:21:32Z";
	//dbus_int32_t discnum = 1;
	//dbus_int32_t usecnt = 0;
	//double userrating = 0;

	DBusMetadata meta[] =
	{
		{ "mpris:trackid",      DBUS_TYPE_STRING_AS_STRING, &trackid,       DBUS_TYPE_STRING,   0 },
		{ "xesam:url",          DBUS_TYPE_STRING_AS_STRING, &songurl,       DBUS_TYPE_STRING,   0 },
		{ "mpris:artUrl",       DBUS_TYPE_STRING_AS_STRING, &arturl,        DBUS_TYPE_STRING,   0 },
	//	{ "xesam:lastused",     DBUS_TYPE_STRING_AS_STRING, &lastused,      DBUS_TYPE_STRING,   0 },
		{ "xesam:genre",        "as",                       &dbusgenre,     DBUS_TYPE_ARRAY,    1 },
		{ "xesam:album",        DBUS_TYPE_STRING_AS_STRING, &utf8album,     DBUS_TYPE_STRING,   0 },
		{ "xesam:title",        DBUS_TYPE_STRING_AS_STRING, &utf8title,     DBUS_TYPE_STRING,   0 },
		{ "mpris:length",       DBUS_TYPE_INT64_AS_STRING,  &songlen,       DBUS_TYPE_INT64,    0 },
		{ "xesam:artist",       "as",                       &dbusartist,    DBUS_TYPE_ARRAY,    1 },
		{ "xesam:composer",     "as",                       &dbusartist,    DBUS_TYPE_ARRAY,    1 },
		{ "xesam:trackNumber",  DBUS_TYPE_INT32_AS_STRING,  &tracknum,      DBUS_TYPE_INT32,    1 },
	//	{ "xesam:discNumber",   DBUS_TYPE_INT32_AS_STRING,  &discnum,       DBUS_TYPE_INT32,    1 },
	//	{ "xesam:useCount",     DBUS_TYPE_INT32_AS_STRING,  &usecnt,        DBUS_TYPE_INT32,    1 },
	//	{ "xesam:userRating",   DBUS_TYPE_DOUBLE_AS_STRING, &userrating,    DBUS_TYPE_DOUBLE,   1 },
		// Extra non-xesam/mpris entries
		{ "vgm:release",        DBUS_TYPE_STRING_AS_STRING, &utf8release,   DBUS_TYPE_STRING,   0 },
		{ "vgm:creator",        DBUS_TYPE_STRING_AS_STRING, &utf8creator,   DBUS_TYPE_STRING,   0 },
		{ "vgm:notes",          DBUS_TYPE_STRING_AS_STRING, &utf8notes,     DBUS_TYPE_STRING,   0 },
		{ "vgm:system",         DBUS_TYPE_STRING_AS_STRING, &utf8system,    DBUS_TYPE_STRING,   0 },
		{ "vgm:version",        DBUS_TYPE_UINT32_AS_STRING, &version,       DBUS_TYPE_UINT32,   0 },
		{ "vgm:loop",           DBUS_TYPE_INT64_AS_STRING,  &looplen,       DBUS_TYPE_INT64,    0 },
		{ "vgm:chips",          "as",                       &chips[0],      DBUS_TYPE_ARRAY,    chips.size() },
	};
	DBusSendMetadataArray(dict_root, meta, sizeof(meta)/sizeof(*meta));
}

static void DBusSendPlaybackStatus(DBusMessageIter* args)
{
	const char* response;

	if(!(mInf->_playState & PLAYSTATE_PLAY))
		response = "Stopped";
	else if(mInf->_playState & PLAYSTATE_PAUSE)
		response = "Paused";
	else
		response = "Playing";

	DBusReplyWithVariant(args, DBUS_TYPE_STRING, DBUS_TYPE_STRING_AS_STRING, &response);
}

static void DBus_EmitSignal(UINT8 type)
{
#ifdef DBUS_DEBUG
	printf("Emitting signal type 0x%x\n", type);
#endif
	if(connection == NULL)
		return;

	// Make sure we're connected to DBus
	// Otherwise discard the event
	if(!dbus_connection_get_is_connected(connection))
		return;

	DBusMessage* msg;
	DBusMessageIter args;

	if(type & SIGNAL_SEEK)
	{
		msg = dbus_message_new_signal(DBUS_MPRIS_PATH, DBUS_MPRIS_PLAYER, "Seeked");

		dbus_message_iter_init_append(msg, &args);
		dbus_int64_t response = Time2USec(mInf->_player.GetCurTime(1));
		dbus_message_iter_append_basic(&args, DBUS_TYPE_INT64, &response);

		dbus_connection_send(connection, msg, NULL);

		dbus_message_unref(msg);

		// Despite Seeked() being a different signal
		// we need to send the changed position property too
		// so we shouldn't return just yet.
	}

	msg = dbus_message_new_signal(DBUS_MPRIS_PATH, DBUS_PROPERTIES, "PropertiesChanged");

	dbus_message_iter_init_append(msg, &args);
	// The interface in which the properties were changed must be sent first
	// Thankfully the only properties changing are in the same interface
	const char* player = DBUS_MPRIS_PLAYER;
	dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &player);
	// Wrap everything inside an a{sv}
	DBusMessageIter dict, dict_entry;

	dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
		if(type & SIGNAL_METADATA)
		{
			// It is possible for the art to change if the playlist contains tracks from multiple games
			// since art can be found by the "Game Name".png field
			// Invalidate it on track change, as it will be populated later on demand
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
				const char* title = "Metadata";
				dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusSendMetadata(&dict_entry);
			dbus_message_iter_close_container(&dict, &dict_entry);
		}
		if(type & SIGNAL_CONTROLS)
		{
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
				const char* title = "CanGoPrevious";
				dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
				DBusAppendCanGoPrevious(&dict_entry);
			dbus_message_iter_close_container(&dict, &dict_entry);
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
				title = "CanGoNext";
				dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
				DBusAppendCanGoNext(&dict_entry);
			dbus_message_iter_close_container(&dict, &dict_entry);
		}
		if(type & SIGNAL_PLAYSTATUS)
		{
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
				const char* playing = "PlaybackStatus";
				dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &playing);
				DBusSendPlaybackStatus(&dict_entry);

			dbus_message_iter_close_container(&dict, &dict_entry);
		}
		if((type & SIGNAL_SEEK) || (type & SIGNAL_PLAYSTATUS))
		{
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
				const char* playing = "Position";
				dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &playing);
				dbus_int64_t response = Time2USec(mInf->_player.GetCurTime(1));
				DBusReplyWithVariant(&dict_entry, DBUS_TYPE_INT64, DBUS_TYPE_INT64_AS_STRING, &response);
			dbus_message_iter_close_container(&dict, &dict_entry);
		}
		if((type & SIGNAL_VOLUME))
		{
			dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
				const char* playing = "Volume";
				dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &playing);
				double response = 1.0;
				DBusReplyWithVariant(&dict_entry, DBUS_TYPE_DOUBLE, DBUS_TYPE_DOUBLE_AS_STRING, &response);
			dbus_message_iter_close_container(&dict, &dict_entry);
		}
	dbus_message_iter_close_container(&args, &dict);

	// Send a blank array _with signature "s"_.
	dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &dict);
	dbus_message_iter_close_container(&args, &dict);

	dbus_connection_send(connection, msg, NULL);
	dbus_message_unref(msg);
}

static void DBusSendMimeTypes(DBusMessageIter* args)
{
	DBusMessageIter variant, subargs;
	dbus_message_iter_open_container(args, DBUS_TYPE_VARIANT, "as", &variant);
		dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &subargs);
			const char* response[] = {"audio/x-vgm", "audio/x-vgz"};
			size_t i_len = sizeof(response) / sizeof(*response);
			for(size_t i = 0; i < i_len; ++i)
			{
				dbus_message_iter_append_basic(&subargs, DBUS_TYPE_STRING, &response[i]);
			}
		dbus_message_iter_close_container(&variant, &subargs);
	dbus_message_iter_close_container(args, &variant);
}

static void DBusSendUriSchemes(DBusMessageIter* args)
{
	DBusMessageIter variant, subargs;
	dbus_message_iter_open_container(args, DBUS_TYPE_VARIANT, "as", &variant);
		dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &subargs);
			const char* response[] = {"file"};
			size_t i_len = sizeof(response) / sizeof(*response);
			for(size_t i = 0; i < i_len; ++i)
			{
				dbus_message_iter_append_basic(&subargs, DBUS_TYPE_STRING, &response[i]);
			}
		dbus_message_iter_close_container(&variant, &subargs);
	dbus_message_iter_close_container(args, &variant);
}

static void DBusSendEmptyMethodResponse(DBusMessage* message)
{
	DBusMessage* reply;
	DBusMessageIter args;
	reply = dbus_message_new_method_return(message);
	dbus_message_iter_init_append(reply, &args);
	dbus_connection_send(connection, reply, NULL);
}

static DBusHandlerResult DBusHandler(DBusConnection* connection, DBusMessage* message, void* user_data)
{
#ifdef DBUS_DEBUG
	const char* interface_name = dbus_message_get_interface(message);
	const char* member_name = dbus_message_get_member(message);
	const char* path_name = dbus_message_get_path(message);
	const char* sender = dbus_message_get_sender(message);

	printf("Interface: %s; Member: %s; Path: %s; Sender: %s;\n", interface_name, member_name, path_name, sender);
#endif
	if(!dbus_message_has_path(message, DBUS_MPRIS_PATH))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	// Respond to Introspect
	if(dbus_message_is_method_call(message, DBUS_INTERFACE_INTROSPECTABLE, "Introspect"))
	{
		DBusReplyToIntrospect(connection, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_MEDIAPLAYER2, "Raise"))
	{
		printf("\a");
		fflush(stdout);
		DBusSendEmptyMethodResponse(message);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	// Respond to Get
	else if(dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Get"))
	{
		char* method_interface_arg = NULL;
		char* method_property_arg  = NULL;
		DBusMessage* reply;

		if(!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &method_interface_arg, DBUS_TYPE_STRING, &method_property_arg, DBUS_TYPE_INVALID))
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

		//printf("Interface name: %s\n", method_interface_arg);
		//init reply
		reply = dbus_message_new_method_return(message);

		//printf("Property name: %s\n", method_property_arg);

		// Global Iterator
		DBusMessageIter args;
		dbus_message_iter_init_append(reply, &args);

		if(!strcmp(method_interface_arg, DBUS_MPRIS_MEDIAPLAYER2))
		{
			if(!strcmp(method_property_arg, "SupportedMimeTypes"))
			{
				DBusSendMimeTypes(&args);
			}
			else if(!strcmp(method_property_arg, "SupportedUriSchemes"))
			{
				DBusSendUriSchemes(&args);
			}
			else if(!strcmp(method_property_arg, "CanQuit"))
			{
				dbus_bool_t response = TRUE;
				DBusReplyWithVariant(&args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "CanRaise"))
			{
				dbus_bool_t response = TRUE;
				DBusReplyWithVariant(&args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "HasTrackList"))
			{
				dbus_bool_t response = FALSE;
				DBusReplyWithVariant(&args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "DesktopEntry"))
			{
				const char* response = "vgmplay";
				DBusReplyWithVariant(&args, DBUS_TYPE_STRING, DBUS_TYPE_STRING_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "Identity"))
			{
				const char* response = "VGMPlay";
				DBusReplyWithVariant(&args, DBUS_TYPE_STRING, DBUS_TYPE_STRING_AS_STRING, &response);
			}
			else
			{
				dbus_message_append_args(reply, DBUS_TYPE_INVALID);
			}
		}
		else if(!strcmp(method_interface_arg, DBUS_MPRIS_PLAYER))
		{
			if(!strcmp(method_property_arg, "CanPlay"))
			{
				dbus_bool_t response = TRUE;
				DBusReplyWithVariant(&args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "CanPause"))
			{
				dbus_bool_t response = TRUE;
				DBusReplyWithVariant(&args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "CanGoNext"))
			{
				DBusAppendCanGoNext(&args);
			}
			else if(!strcmp(method_property_arg, "CanGoPrevious"))
			{
				DBusAppendCanGoPrevious(&args);
			}
			else if(!strcmp(method_property_arg, "CanSeek"))
			{
				dbus_bool_t response = TRUE;
				DBusReplyWithVariant(&args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "PlaybackStatus"))
			{
				DBusSendPlaybackStatus(&args);
			}
			else if(!strcmp(method_property_arg, "Position"))
			{
				dbus_int64_t response = Time2USec(mInf->_player.GetCurTime(1));
				DBusReplyWithVariant(&args, DBUS_TYPE_INT64, DBUS_TYPE_INT64_AS_STRING, &response);
			}
			//Dummy volume
			else if(!strcmp(method_property_arg, "Volume") || !strcmp(method_property_arg, "MaximumRate") || !strcmp(method_property_arg, "MinimumRate") || !strcmp(method_property_arg, "Rate"))
			{
				double response = 1.0;
				DBusReplyWithVariant(&args, DBUS_TYPE_DOUBLE, DBUS_TYPE_DOUBLE_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "CanControl"))
			{
				dbus_bool_t response = TRUE;
				DBusReplyWithVariant(&args, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &response);
			}
			else if(!strcmp(method_property_arg, "Metadata"))
			{
				DBusSendMetadata(&args);
			}
			else
			{
				dbus_message_append_args(reply, DBUS_TYPE_INVALID);
			}
		}
		else
		{
#ifdef DBUS_DEBUG
			printf("Unimplemented interface %s passed to Get()\n", method_interface_arg);
#endif
			dbus_message_unref(reply);
			reply = dbus_message_new_error(message, "org.freedesktop.DBus.Error.InvalidArgs", "No such interface");
		}

		dbus_connection_send(connection, reply, NULL);
		dbus_message_unref(reply);

		return DBUS_HANDLER_RESULT_HANDLED;
	}
	// Respond to GetAll
	else if(dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "GetAll"))
	{
		char* method_interface_arg = NULL;
		DBusMessage* reply;

		if(!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &method_interface_arg, DBUS_TYPE_INVALID))
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

		//init reply
		reply = dbus_message_new_method_return(message);

		//printf("Property name: %s\n", property_name);

		// Global Iterator
		DBusMessageIter args;
		dbus_message_iter_init_append(reply, &args);

		dbus_bool_t dbustrue = TRUE;
		dbus_bool_t dbusfalse = FALSE;
		const char* title;
		const char* strresponse;

		if(!strcmp(method_interface_arg, DBUS_MPRIS_MEDIAPLAYER2))
		{
			// a{sv}
			DBusMessageIter dict, dict_entry;
			dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);

				// Open Dict
				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "SupportedMimeTypes";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusSendMimeTypes(&dict_entry);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "SupportedUriSchemes";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusSendUriSchemes(&dict_entry);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanQuit";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &dbustrue);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanRaise";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &dbustrue);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "HasTrackList";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &dbusfalse);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "DesktopEntry";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					strresponse = "vgmplay";
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_STRING, DBUS_TYPE_STRING_AS_STRING, &strresponse);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "Identity";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					strresponse = "VGMPlay";
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_STRING, DBUS_TYPE_STRING_AS_STRING, &strresponse);
				dbus_message_iter_close_container(&dict, &dict_entry);

			dbus_message_iter_close_container(&args, &dict);
		}
		else if(!strcmp(method_interface_arg, DBUS_MPRIS_PLAYER))
		{
			double doubleresponse = 1.0;
			// a{sv}
			DBusMessageIter dict, dict_entry;
			dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanControl";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &dbustrue);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanGoNext";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusAppendCanGoNext(&dict_entry);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanGoPrevious";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusAppendCanGoPrevious(&dict_entry);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanPause";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &dbustrue);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanPlay";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &dbustrue);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "CanSeek";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_BOOLEAN, DBUS_TYPE_BOOLEAN_AS_STRING, &dbustrue);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "Metadata";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
						DBusSendMetadata(&dict_entry);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "MaximumRate";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_DOUBLE, DBUS_TYPE_DOUBLE_AS_STRING, &doubleresponse);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "MinimumRate";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_DOUBLE, DBUS_TYPE_DOUBLE_AS_STRING, &doubleresponse);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "Rate";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_DOUBLE, DBUS_TYPE_DOUBLE_AS_STRING, &doubleresponse);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "Volume";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_DOUBLE, DBUS_TYPE_DOUBLE_AS_STRING, &doubleresponse);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "Position";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					dbus_int64_t position = Time2USec(mInf->_player.GetCurTime(1));
					DBusReplyWithVariant(&dict_entry, DBUS_TYPE_INT64, DBUS_TYPE_INT64_AS_STRING, &position);
				dbus_message_iter_close_container(&dict, &dict_entry);

				dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
					// Field Title
					title = "PlaybackStatus";
					dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title);
					DBusSendPlaybackStatus(&dict_entry);
				dbus_message_iter_close_container(&dict, &dict_entry);

			dbus_message_iter_close_container(&args, &dict);
		}
		else
		{
#ifdef DBUS_DEBUG
			printf("Unimplemented interface %s passed to GetAll\n", method_interface_arg);
#endif
			dbus_message_unref(reply);
			reply = dbus_message_new_error(message, "org.freedesktop.DBus.Error.InvalidArgs", "No such interface");
		}

		dbus_connection_send(connection, reply, NULL);
		dbus_message_unref(reply);

		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "Seek"))
	{
		dbus_int64_t offset = 0;
		if(!dbus_message_get_args(message, NULL, DBUS_TYPE_INT64, &offset, DBUS_TYPE_INVALID))
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

		DBusEmptyMethodResponse(connection, message);
#ifdef DBUS_DEBUG
		printf("Seek called with %lld\n", (long long)offset);
#endif
		mInf->Event(MI_EVT_SEEK_REL, USec2Samples(offset, mInf->_player));
		// the "seeked" signal will be emitted automatically by the player
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "Play"))
	{
		DBusEmptyMethodResponse(connection, message);
		mInf->Event(MI_EVT_PAUSE, MIE_PS_RESUME);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "Pause"))
	{
		DBusEmptyMethodResponse(connection, message);
		mInf->Event(MI_EVT_PAUSE, MIE_PS_PAUSE);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "PlayPause"))
	{
		DBusEmptyMethodResponse(connection, message);
		mInf->Event(MI_EVT_PAUSE, MIE_PS_TOGGLE);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "Stop"))
	{
		DBusEmptyMethodResponse(connection, message);
		mInf->Event(MI_EVT_CONTROL, MIE_CTRL_STOP);	// currently no effect
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "Previous"))
	{
		DBusEmptyMethodResponse(connection, message);
		mInf->Event(MI_EVT_PLIST, MIE_PL_PREV);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "Next"))
	{
		DBusEmptyMethodResponse(connection, message);
		mInf->Event(MI_EVT_PLIST, MIE_PL_NEXT);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_MPRIS_PLAYER, "SetPosition"))
	{
		dbus_int64_t pos;
		const char* path;
		if(!dbus_message_get_args(message, NULL, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INT64, &pos, DBUS_TYPE_INVALID))
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

		DBusEmptyMethodResponse(connection, message);
		mInf->Event(MI_EVT_SEEK_ABS, USec2Samples(pos, mInf->_player));
		// the "seeked" signal will be emitted automatically by the player
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else if(dbus_message_is_method_call(message, DBUS_INTERFACE_PROPERTIES, "Set"))
	{
		// Dummy Set to send a signal to revert Volume change attempts
		DBus_EmitSignal(SIGNAL_VOLUME);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	else
	{
#ifdef DBUS_DEBUG
		printf("Method %s for interface %s not implemented", member_name, interface_name);
#endif
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
}

UINT8 MediaControl::Init(MediaInfo& mediaInfo)
{
	mInf = &mediaInfo;
	mInf->_enableAlbumImage = true;

	connection = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if(!connection)
		return 0x00;

	// If we're not the owners, don't bother with anything else
	if(dbus_bus_request_name(connection, DBUS_MPRIS_VGMPLAY, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
	{
		dbus_connection_unref(connection);
		connection = NULL;
		return 0x00;
	}

	DBusObjectPathVTable vtable =
	{
		.unregister_function = NULL,
		.message_function = DBusHandler,
	};

	dbus_connection_try_register_object_path(connection, DBUS_MPRIS_PATH, &vtable, NULL, NULL);
	
	mInf->AddSignalCallback(&MediaControl::SignalCB, this);

	return 0x00;
}

void MediaControl::Deinit(void)
{
	if(connection != NULL)
		dbus_connection_unref(connection);
}

void MediaControl::ReadWriteDispatch(void)
{
	if(connection == NULL)
		return;

	// Wait at most for 1ms
	dbus_connection_read_write_dispatch(connection, 1);
}

/*static*/ void MediaControl::SignalCB(MediaInfo* mInfo, void* userParam, UINT8 signalMask)
{
	MediaControl* obj = static_cast<MediaControl*>(userParam);
	obj->SignalHandler(signalMask);
	return;
}

void MediaControl::SignalHandler(UINT8 signalMask)
{
	UINT8 dbusSignal = 0x00;
	if (signalMask & MI_SIG_NEW_SONG)
		dbusSignal |= SIGNAL_SEEK | SIGNAL_METADATA | SIGNAL_PLAYSTATUS | SIGNAL_CONTROLS;
	if (signalMask & MI_SIG_PLAY_STATE)
		dbusSignal |= SIGNAL_PLAYSTATUS;
	if (signalMask & MI_SIG_POSITION)
		dbusSignal |= SIGNAL_SEEK;
	if (signalMask & MI_SIG_VOLUME)
		dbusSignal |= SIGNAL_CONTROLS;
	DBus_EmitSignal(dbusSignal);
	return;
}
