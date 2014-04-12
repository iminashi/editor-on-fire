#include <allegro.h>
#include <stdio.h>
#include <ctype.h>
#include "song.h"
#include "utility.h"	//For eof_buffer_file()
#include "ini.h"		//For eof_difficulty_ini_tags[]
#include "main.h"		//For logging
#include "ini_import.h"
#include "undo.h"
#include "foflc/Lyric_storage.h"	//For strcasestr_spec()
#include "menu/song.h"	//For eof_is_number()

#ifdef USEMEMWATCH
#include "memwatch.h"
#endif

typedef struct
{

	char type[256];
	char value[1024];

} EOF_IMPORT_INI_SETTING;

#define EOF_MAX_INI_IMPORTED_LINES 100
EOF_IMPORT_INI_SETTING eof_import_ini_setting[EOF_MAX_INI_IMPORTED_LINES];
int eof_import_ini_settings = 0;

char eof_ini_pro_drum_tag_present;		//Is set to nonzero if eof_import_ini() finds the "pro_drums = True" tag (to influence MIDI import)
char eof_ini_star_power_tag_present;	//Is set to nonzero if eof_import_ini() finds the "multiplier_note = 116" tag (to influence MIDI import)
char eof_ini_sysex_open_bass_present;	//Is set to nonzero if eof_import_ini() finds the "sysex_open_bass = True" tag (to influence MIDI import)

/* it would probably be easier to use Allegro's configuration routines to read
 * the ini files since it looks like they are formatted correctly */
int eof_import_ini(EOF_SONG * sp, char * fn, int function)
{
	char * textbuffer = NULL;
	char * line_token = NULL;
	char * token;
	char * equals = NULL;
	int i;
	int j;
	unsigned long stringlen, tracknum;
	char setting_stored;
	char *value_index;
	char status;

	eof_log("eof_import_ini() entered", 1);

	eof_ini_pro_drum_tag_present = 0;	//Reset this condition to false
	eof_ini_star_power_tag_present = 0;	//Reset this condition to false
	eof_ini_sysex_open_bass_present = 0;//Reset this condition to false

	if(!sp || !fn)
	{
		return 0;
	}
	eof_log("\tBuffering INI file into memory", 1);
	eof_import_ini_settings = 0;
	textbuffer = (char *)eof_buffer_file(fn, 1);	//Buffer the INI file into memory, adding a NULL terminator at the end of the buffer
	if(!textbuffer)
	{
		eof_log("\tCannot open INI file, skipping", 1);
		return 0;
	}
	eof_log("\tTokenizing INI file buffer", 1);
	(void) ustrtok(textbuffer, "\r\n");
	eof_log("\tParsing INI file buffer", 1);
	while(1)
	{	//While the INI file hasn't been exhausted
		line_token = ustrtok(NULL, "\r\n");	//Return the next line
		if(!line_token)
		{	//If there isn't another line
			break;
		}
		else
		{	//If there is another line
			if(ustrlen(line_token) > 6)
			{	//If the line is at least 7 characters long
				/* find the first '=' */
				for(i = 0; i < ustrlen(line_token); i++)
				{
					if(ugetc(&line_token[uoffset(line_token, i)]) == '=')
					{
						equals = &line_token[uoffset(line_token, i)];
						break;
					}
				}

				/* if this line has an '=', process line as a setting */
				if(equals)
				{
					if(eof_import_ini_settings == EOF_MAX_INI_IMPORTED_LINES)
						break;	//Break from while loop if the eof_import_ini_setting[] array is full

					equals[0] = '\0';
					token = equals + 1;
					(void) ustrncpy(eof_import_ini_setting[eof_import_ini_settings].type, line_token, 256-1);
					(void) ustrncpy(eof_import_ini_setting[eof_import_ini_settings].value, token, 1024-1);
					while(1)
					{	//Drop all trailing space characters from the tag type string
						stringlen = ustrlen(eof_import_ini_setting[eof_import_ini_settings].type);
						if(stringlen < 1)
							break;
						if(eof_import_ini_setting[eof_import_ini_settings].type[stringlen - 1] == ' ')
						{	//If the last character in this tag type is a space character, truncate it to simplify the tag matching below
							eof_import_ini_setting[eof_import_ini_settings].type[stringlen - 1] = '\0';
						}
						else
							break;
					}
					if(eof_import_ini_setting[eof_import_ini_settings].type[0] != '\0')
					{	//If the type string wasn't just space characters up to the equal sign, add the INI setting
						eof_import_ini_settings++;
					}
				}
			}//If the line is at least 7 characters long
		}//If there is another line
	}//While the INI file hasn't been exhausted
	eof_log("\tProcessing INI file contents", 1);
	for(i = 0; i < eof_import_ini_settings; i++)
	{	//For each imported INI setting
		if(eof_import_ini_setting[i].type[0] == '\0')
			continue;	//Skip this INI type if its string is empty

		value_index = eof_import_ini_setting[i].value;	//Prepare to skip leading whitespace
		while((*value_index != '\0') && (*value_index == ' '))
		{	//If this is a space character
			value_index++;	//Point to the next character
		}

		if(*value_index != '\0')
		{	//If the value portion of the entry has content
			if(!ustricmp(eof_import_ini_setting[i].type, "artist"))
			{
				if(!ustricmp(value_index, "Unknown Artist"))
				{	//If this is EOF's placeholder for an undefined artist field
					if(ustricmp(sp->tags->artist, "Unknown Artist"))
					{	//If the project doesn't explicitly list this as the artist field
						value_index[0] = '\0';	//Make it an empty string so it can be compared against the string already in the project
					}
				}
				if(eof_compare_set_ini_string_field(sp->tags->artist, value_index, 256-1, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "name"))
			{
				if(!ustricmp(value_index, "Untitled"))
				{	//If this is EOF's placeholder for an undefined song title field
					if(ustricmp(sp->tags->title, "Untitled"))
					{	//If the project doesn't explicitly list this as the song title field
						value_index[0] = '\0';	//Make it an empty string so it can be compared against the string already in the project
					}
				}
				if(eof_compare_set_ini_string_field(sp->tags->title, value_index, 256-1, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "frets"))
			{
				if(eof_compare_set_ini_string_field(sp->tags->frettist, value_index, 256-1, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "album"))
			{
				if(eof_compare_set_ini_string_field(sp->tags->album, value_index, 256-1, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "year"))
			{
				unsigned long index;
				if(eof_is_number(value_index))
				{	//If the number is a valid year (all numerical characters)
					value_index[4] = '\0';	//Ensure the number is truncated to 4 characters
					if(eof_compare_set_ini_string_field(sp->tags->year, value_index, 32-1, &function, eof_import_ini_setting[i].type))
					{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
						free(textbuffer);	//Free buffered INI file from memory
						return 0;
					}
				}
				else
				{		//If there are non numerical characters
					if(eof_compare_set_ini_string_setting(sp, "year", value_index, &function, eof_import_ini_setting[i].type))	//Add the year as a custom INI setting
					{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
						free(textbuffer);	//Free buffered INI file from memory
						return 0;
					}
				}
				if(eof_find_ini_setting_tag(sp, &index, "year") && (sp->tags->year[0] != '\0'))
				{	//If the project has both a "year" custom INI setting AND a "year" numerical setting
					allegro_message("Warning:  This project contains both a numerical and a non numerical year tag, one should be removed manually");
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "loading_phrase"))
			{
				if(eof_compare_set_ini_string_field(sp->tags->loading_text, value_index, 512-1, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "lyrics"))
			{
				if(eof_compare_set_ini_boolean(&status, sp->tags->lyrics, value_index, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
				sp->tags->lyrics = status;
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "eighthnote_hopo"))
			{
				if(eof_compare_set_ini_boolean(&status, sp->tags->eighth_note_hopo, value_index, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
				sp->tags->eighth_note_hopo = status;
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "delay"))
			{
				long value;
				if(eof_compare_set_ini_integer(&value, sp->tags->ogg[eof_selected_ogg].midi_offset, value_index, &function, eof_import_ini_setting[i].type))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
				if(value < 0)
				{	//If the converted MIDI delay was negative
					value = 0;
				}
				sp->tags->ogg[0].midi_offset = value;
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "score"))
			{
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "scores"))
			{
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "scores_ext"))
			{
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "real_guitar_tuning"))
			{
				unsigned long tracknum = sp->track[EOF_TRACK_PRO_GUITAR]->tracknum;			//The 22 fret pro guitar track if it is populated, otherwise the 17 fret track if it is populated
				if(eof_compare_set_ini_pro_guitar_tuning(sp->pro_guitar_track[tracknum], value_index, &function))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "real_guitar_22_tuning"))
			{
				unsigned long tracknum = sp->track[EOF_TRACK_PRO_GUITAR_22]->tracknum;
				if(eof_compare_set_ini_pro_guitar_tuning(sp->pro_guitar_track[tracknum], value_index, &function))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "real_bass_tuning"))
			{
				unsigned long tracknum = sp->track[EOF_TRACK_PRO_BASS]->tracknum;
				if(eof_compare_set_ini_pro_guitar_tuning(sp->pro_guitar_track[tracknum], value_index, &function))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "real_bass_22_tuning"))
			{
				unsigned long tracknum = sp->track[EOF_TRACK_PRO_BASS_22]->tracknum;
				if(eof_compare_set_ini_pro_guitar_tuning(sp->pro_guitar_track[tracknum], value_index, &function))
				{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
					free(textbuffer);	//Free buffered INI file from memory
					return 0;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "pro_drums"))
			{
				int func = 0;
				(void) eof_compare_set_ini_boolean(&status, 0, value_index, &func, eof_import_ini_setting[i].type);	//Check if this tag's value is "True" or "1"
				if(status)
				{
					eof_ini_pro_drum_tag_present = 1;
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "five_lane_drums"))
			{
				int func = 0;
				(void) eof_compare_set_ini_boolean(&status, 0, value_index, &func, eof_import_ini_setting[i].type);	//Check if this tag's value is "True" or "1"
				if(status)
				{
					tracknum = sp->track[EOF_TRACK_DRUM]->tracknum;
					sp->track[EOF_TRACK_DRUM]->flags |= EOF_TRACK_FLAG_SIX_LANES;	//Set the five lane drum flag
					sp->legacy_track[tracknum]->numlanes = 6;						//Set the lane count
					tracknum = sp->track[EOF_TRACK_DRUM_PS]->tracknum;
					sp->track[EOF_TRACK_DRUM_PS]->flags |= EOF_TRACK_FLAG_SIX_LANES;	//Set the five lane drum flag for the PS drum track
					sp->legacy_track[tracknum]->numlanes = 6;							//Set the lane count
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "multiplier_note"))
			{
				int func = 0;
				(void) eof_compare_set_ini_boolean(&status, 0, value_index, &func, eof_import_ini_setting[i].type);	//Check if this tag's value is "True" or "1"
				if(status)
				{
					eof_ini_star_power_tag_present = 1;	//MIDI import won't have to convert solos phrases to star power, EOF's notation for star power style phrases was found
				}
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "sysex_open_bass"))
			{
				int func = 0;
				(void) eof_compare_set_ini_boolean(&status, 0, value_index, &func, eof_import_ini_setting[i].type);	//Check if this tag's value is "True" or "1"
				if(status)
				{
					eof_ini_sysex_open_bass_present = 1;	//MIDI import will interpret forced HOPO lane 1 bass to be a HOPO bass gem and not an open strum
				}
			}

			//Other tags ignored by INI import because they are only informational about EOF's exported MIDIs
			else if(!ustricmp(eof_import_ini_setting[i].type, "sysex_pro_slide") || !ustricmp(eof_import_ini_setting[i].type, "sysex_high_hat_ctrl") || !ustricmp(eof_import_ini_setting[i].type, "sysex_rimshot") || !ustricmp(eof_import_ini_setting[i].type, "sysex_slider"))
			{	//These Sysex indicators are only used by Phase Shift
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "star_power_note"))
			{	//Used by Phase Shift
			}
			else if(!ustricmp(eof_import_ini_setting[i].type, "song_length"))
			{	//Used by one or more unnamed rhythm games
			}

			/* for custom settings or difficulty strings */
			else
			{
				setting_stored = 0;
				for(j = 0; (j < EOF_TRACKS_MAX) && (j < sp->tracks); j++)
				{	//For each string in the eof_difficulty_ini_tags[] array (for each currently supported track number)
					if(eof_difficulty_ini_tags[j] && !ustricmp(eof_import_ini_setting[i].type, eof_difficulty_ini_tags[j]))
					{	//If this INI setting matches the difficulty tag, store the difficulty value into the appropriate track structure
						long value;
						long original = sp->track[j]->difficulty;
						if(original == 0xFF)
						{	//If the project does not have this difficulty defined
							original = -1;	//Convert it to -1 so it can be accurately compared to the value in the INI file
						}
						if(atoi(value_index) >= 0)
						{	//Only store the difficulty if it isn't negative (-1 means empty track)
							if(eof_compare_set_ini_integer(&value, original, value_index, &function, eof_import_ini_setting[i].type))
							{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
								free(textbuffer);	//Free buffered INI file from memory
								return 0;
							}
							if((value < 0) || (value > 6))		//If the difficulty is invalid
								value = 0xFF;					//Reset to undefined
							sp->track[j]->difficulty = value;
						}
						setting_stored = 1;	//Consider this INI tag handled (if it was -1, it will be dropped instead of being stored as a custom INI tag)
						break;
					}
				}
				if(!setting_stored)
				{
					if(!ustricmp(eof_import_ini_setting[i].type, "diff_drums_real"))
					{	//If this is a pro drum difficulty tag
						long value;
						long original = (sp->track[EOF_TRACK_DRUM]->flags & 0x0F000000) >> 24;
						if(original == 0xF)
						{	//If the project does not have this difficulty defined
							original = -1;	//Convert it to -1 so it can be accurately compared to the value in the INI file
						}
						if(eof_compare_set_ini_integer(&value, original, value_index, &function, eof_import_ini_setting[i].type))
						{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
							free(textbuffer);	//Free buffered INI file from memory
							return 0;
						}
						if((value < 0) || (value > 6))		//If the difficulty is invalid
							value = 0xF;					//Reset to undefined
						sp->track[EOF_TRACK_DRUM]->flags &= ~(0x0F << 24);	//Clear the lower nibble of the drum track's flag's most significant byte
						sp->track[EOF_TRACK_DRUM]->flags |= ((unsigned long)value << 24);	//Store the pro drum difficulty in the drum track's flag's most significant byte
					}
					else if(!ustricmp(eof_import_ini_setting[i].type, "diff_vocals_harm"))
					{	//If this is a harmony difficulty tag
						long value;
						long original = (sp->track[EOF_TRACK_VOCALS]->flags & 0x0F000000) >> 24;
						if(original == 0xF)
						{	//If the project does not have this difficulty defined
							original = -1;	//Convert it to -1 so it can be accurately compared to the value in the INI file
						}
						if(eof_compare_set_ini_integer(&value, original, value_index, &function, eof_import_ini_setting[i].type))
						{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
							free(textbuffer);	//Free buffered INI file from memory
							return 0;
						}
						if((value < 0) || (value > 6))		//If the difficulty is invalid
							value = 0xF;					//Reset to undefined
						sp->track[EOF_TRACK_VOCALS]->flags &= ~(0x0F << 24);	//Clear the low nibble of the vocal track's flag's most significant byte
						sp->track[EOF_TRACK_VOCALS]->flags |= ((unsigned long)value << 24);	//Store the pro drum difficulty in the drum track's flag's most significant byte
					}
					else if(!ustricmp(eof_import_ini_setting[i].type, "diff_band"))
					{	//If this is a band difficulty tag
						long value;
						if(eof_compare_set_ini_integer(&value, sp->tags->difficulty, value_index, &function, eof_import_ini_setting[i].type))
						{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
							free(textbuffer);	//Free buffered INI file from memory
							return 0;
						}
						if((value < 0) || (value > 6))		//If the difficulty is invalid
							value = 0xFF;					//Reset to undefined
						sp->tags->difficulty = value;
					}
					else
					{	//Store it as a custom INI setting
						if(eof_compare_set_ini_string_setting(sp, eof_import_ini_setting[i].type, value_index, &function, eof_import_ini_setting[i].type))
						{	//If the INI file is being merged with the project and the user did not want the project's setting replaced
							free(textbuffer);	//Free buffered INI file from memory
							return 0;
						}
					}
				}
			}
		}//If the value portion of the entry has content
	}//For each imported INI setting
	eof_log("\tFreeing INI buffer", 1);
	free(textbuffer);	//Free buffered INI file from memory
	return 1;
}

int eof_compare_set_ini_string_field(char *dest, char *src, unsigned long maxchars, int *function, char *tag)
{
	if(!dest || !src || !function || !tag)
		return 1;	//Return error

	if(ustricmp(dest, src))
	{	//If the strings don't match
		if(*function)
		{	//If the calling function wanted to check for differences and prompt the user to overwrite
			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "Song property \"%s\" altered in INI file", tag);
			eof_log(eof_log_string, 1);
			eof_clear_input();
			key[KEY_Y] = 0;
			key[KEY_N] = 0;
			if(alert("Warning:  The INI file has been externally edited.", "Merge its changes with the active project?", NULL, "&Yes", "&No", 'y', 'n') != 1)
			{	//If the user did not opt to merge the changes into the project
				return 1;	//Return user cancellation
			}
			eof_prepare_undo(EOF_UNDO_TYPE_NONE);
			*function = 0;	//Disable any further user prompting regarding this INI file
		}

		(void) ustrncpy(dest, src, (int)maxchars);	//Copy the string
	}
	return 0;
}

int eof_compare_set_ini_boolean(char *status, char original, char *string, int *function, char *tag)
{
	if(!status || !string || !function || !tag)
		return 1;	//Return error

	if(!ustricmp(string, "True") || !ustricmp(string, "1"))
	{	//If the string indicates a true status
		*status = 1;
	}
	else
	{
		*status = 0;
	}

	if(*function && (*status != original))
	{	//If the determined boolean status does not match the supplied original value, and the calling function wanted to prompt the user in such a case
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "Song boolean \"%s\" altered in INI file", tag);
		eof_log(eof_log_string, 1);
		eof_clear_input();
		key[KEY_Y] = 0;
		key[KEY_N] = 0;
		if(alert("Warning:  The INI file has been externally edited.", "Merge its changes with the active project?", NULL, "&Yes", "&No", 'y', 'n') != 1)
		{	//If the user did not opt to merge the changes into the project
			return 1;	//Return user cancellation
		}
		eof_prepare_undo(EOF_UNDO_TYPE_NONE);
		*function = 0;	//Disable any further user prompting regarding this INI file
	}
	return 0;
}

int eof_compare_set_ini_string_setting(EOF_SONG *sp, char *tag, char *value, int *function, char *logtag)
{
	unsigned long index;
	char *ptr;
	char alter = 0, add = 0;

	if(!sp || !tag || !value || !function)
		return 1;	//Return error

	index = sp->tags->ini_settings;	//If no match is found, the setting will be appended to the list
	ptr = eof_find_ini_setting_tag(sp, &index, tag);	//Find the specified INI tag in the project if it exists
	if(ptr)
	{	//If the target INI setting was found in the project
		for(;(*ptr != '\0') && isspace(*ptr);ptr++);	//Skip whitespace following the equal sign
		if(ustricmp(ptr, value))
		{	//If the INI setting's tag value doesn't match the value specified
			alter = 1;	//Note that an existing INI setting will be altered
		}
	}
	else
	{	//If the INI setting was not in the project
		add = 1;	//Note that a new INI setting will be added
	}

	if(alter || add)
	{	//If any INI setting is being altered or added
		if(alter)
		{
			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "Song INI setting \"%s\" altered in INI file", logtag);
		}
		else
		{
			(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "Song INI setting \"%s\" added in INI file", logtag);
		}
		eof_log(eof_log_string, 1);
		if(*function)
		{	//If the calling function wanted to prompt the user before changing/adding an INI setting
			eof_clear_input();
			key[KEY_Y] = 0;
			key[KEY_N] = 0;
			if(alert("Warning:  The INI file has been externally edited.", "Merge its changes with the active project?", NULL, "&Yes", "&No", 'y', 'n') != 1)
			{	//If the user did not opt to merge the changes into the project
				return 1;	//Return user cancellation
			}
			eof_prepare_undo(EOF_UNDO_TYPE_NONE);
			*function = 0;	//Disable any further user prompting regarding this INI file
		}

		//Replace the existing INI setting or append it to the list of INI settings as appropriate
		if(index < EOF_MAX_INI_SETTINGS)
		{	//If the maximum number of INI settings isn't already defined
			(void) snprintf(sp->tags->ini_setting[index], sizeof(sp->tags->ini_setting[index]) - 1, "%s = %s", tag, value);
			if(index >= sp->tags->ini_settings)
			{	//If this was a newly-added setting
				sp->tags->ini_settings++;	//Increment the counter
			}
		}
	}

	return 0;
}

int eof_compare_set_ini_pro_guitar_tuning(EOF_PRO_GUITAR_TRACK *tp, char *string, int *function)
{
	unsigned long ctr = 0, ctr2;
	char tuning[EOF_TUNING_LENGTH] = {0};
	char * line_token = NULL;

	if(!tp || !string || !function)
		return 1;	//Return error

	line_token = ustrtok(string, " \r\n");	//Find first token (string of characters that isn't whitespace, carriage return or newline)
	while(line_token != NULL)
	{	//For each string tuning that is parsed
		if(line_token[0] == '"')
			break;	//Stop parsing if the tuning name string has been reached
		if(ctr >= EOF_TUNING_LENGTH)
			break;	//Do not read more string tunings than are supported
		tuning[ctr] = atol(line_token) % 12;	//Convert the string to an integer value
		ctr++;	//Increment the counter
		line_token = ustrtok(NULL, " \r\n");	//Find next token
	}

	if(*function)
	{	//If the calling function wanted to check for differences and prompt the user to overwrite
		char changes = 0;
		if((ctr < 4 || ctr > 6))
		{	//If the number of strings defined isn't supported
			allegro_message("Warning:  Invalid pro guitar tuning tag.  Reverting to 6 string standard tuning.");
			ctr = 6;
			memset(tuning, 0, EOF_TUNING_LENGTH);
		}
		for(ctr2 = 0; ctr2 < ctr; ctr2++)
		{	//For each string tuning parsed
			if(tuning[ctr2] != tp->tuning[ctr2])
			{	//If this tuning is different from what's already in the project
				changes = 1;
				break;
			}
		}
		if(changes || (ctr != tp->numstrings))
		{	//If the INI tag defined a different number of strings, or a different tuning for any of them
			eof_log("Pro guitar/bass tuning tag altered in INI file", 1);
			eof_clear_input();
			key[KEY_Y] = 0;
			key[KEY_N] = 0;
			if(alert("Warning:  The INI file has been externally edited.", "Merge its changes with the active project?", NULL, "&Yes", "&No", 'y', 'n') != 1)
			{	//If the user did not opt to merge the changes into the project
				return 1;	//Return user cancellation
			}
			eof_prepare_undo(EOF_UNDO_TYPE_NONE);
		}
		*function = 0;	//Disable any further user prompting regarding this INI file
	}

	tp->numstrings = ctr;	//Define the number of strings in the track based on the tuning tag
	memcpy(tp->tuning, tuning, EOF_TUNING_LENGTH);	//Copy the tuning array
	return 0;
}

int eof_compare_set_ini_integer(long *value, long original, char *string, int *function, char *tag)
{
	if(!value || !string || !function)
		return 1;	//Return error

	*value = atoi(string);

	if(*function && (*value != original))
	{	//If the converted number does not match the supplied original value, and the calling function wanted to prompt the user in such a case
		(void) snprintf(eof_log_string, sizeof(eof_log_string) - 1, "Song property \"%s\" altered in INI file", tag);
		eof_log(eof_log_string, 1);
		eof_clear_input();
		key[KEY_Y] = 0;
		key[KEY_N] = 0;
		if(alert("Warning:  The INI file has been externally edited.", "Merge its changes with the active project?", NULL, "&Yes", "&No", 'y', 'n') != 1)
		{	//If the user did not opt to merge the changes into the project
			return 1;	//Return user cancellation
		}
		eof_prepare_undo(EOF_UNDO_TYPE_NONE);
		*function = 0;	//Disable any further user prompting regarding this INI file
	}
	return 0;
}

char *eof_find_ini_setting_tag(EOF_SONG *sp, unsigned long *index, char *tag)
{
	unsigned long ctr;
	char buffer[512] = {0};
	char *ptr;

	if(!sp || !index || !tag)
		return NULL;	//Return error

	(void) snprintf(buffer, sizeof(buffer) - 1, "%s =", tag);	//Build the left half of the string that results from this INI setting
	for(ctr = 0; ctr < sp->tags->ini_settings; ctr++)
	{	//For each INI setting in the project
		ptr = strcasestr_spec(sp->tags->ini_setting[ctr], buffer);	//If this INI setting matches the specified tag, get the address of the first character after the equal sign
		if(ptr)
		{	//If this INI setting contains the specified tag
			*index = ctr;	//Store the INI setting number containing this tag
			return ptr;
		}
	}

	return NULL;	//No match found
}
