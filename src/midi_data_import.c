#include <allegro.h>
#include "main.h"
#include "midi_data_import.h"
#include "midi_import.h"

#ifdef USEMEMWATCH
#include "memwatch.h"
#endif

#define MIDI_DATA_IMPORT_DEBUG

double eof_MIDI_delta_to_realtime(struct eof_MIDI_tempo_change *tempolist, unsigned long absdelta, unsigned timedivision)
{
	struct eof_MIDI_tempo_change *ptr = tempolist;
	double BPM = 120.0;
	unsigned long reldelta = absdelta;
	double time = 0.0;	//Will store the time of the tempo change immediately prior to the specified absolute delta time

	if(tempolist)
	{	//If there are tempo changes in the list
		while(ptr->next != NULL)
		{	//While there are more tempo changes
			if(absdelta >= (ptr->next)->absdelta)
			{	//If this event starts at or after the next tempo change
				ptr = ptr->next;	//Advance to the next tempo change
			}
			else
			{	//Otherwise break from the loop
				break;
			}
		}
		BPM = ptr->BPM;
		time = ptr->realtime;
		reldelta = absdelta - ptr->absdelta;
	}

	return time + (double)reldelta / timedivision * ((double)60000.0 / BPM);	//Calculate the number of milliseconds between the specified delta time and the prior tempo change, and add it to the time of that tempo change (if it exists)
}

void eof_MIDI_empty_event_list(struct eof_MIDI_data_event *ptr)
{
	struct eof_MIDI_data_event *temp;

	while(ptr != NULL)
	{
		temp = ptr->next;
		if(ptr->data)
			free(ptr->data);
		free(ptr);
		ptr = temp;
	}
}

void eof_MIDI_empty_track_list(struct eof_MIDI_data_track *ptr)
{
	struct eof_MIDI_data_track *temp;

	while(ptr != NULL)
	{
		temp = ptr->next;
		if(ptr->trackname)
			free(ptr->trackname);
		eof_MIDI_empty_event_list(ptr->events);
		free(ptr);
		ptr = temp;
	}
}

void eof_MIDI_empty_tempo_list(struct eof_MIDI_tempo_change *ptr)
{
	struct eof_MIDI_tempo_change *temp;

	while(ptr != NULL)
	{
		temp = ptr->next;
		free(ptr);
		ptr = temp;
	}
}

struct eof_MIDI_data_track *eof_get_raw_MIDI_data(MIDI *midiptr, unsigned tracknum)
{
	unsigned long delta, absdelta, reldelta, length, bytes_used;
	unsigned int size, curtrack, ctr;
	int track_pos, event_pos;
	unsigned char runningstatus, eventtype, lasteventtype, meventtype, endreached, *dataptr;
	struct eof_MIDI_data_event *head = NULL, *tail = NULL, *linkptr = NULL;
	struct eof_MIDI_tempo_change *tempohead = NULL, *tempotail = NULL, *tempoptr = NULL;
	struct eof_MIDI_data_track *trackptr = NULL;
	double currentbpm = 120.0;	//As per the MIDI specification, until a tempo change is reached, 120BPM is assumed
	double realtime = 0.0;
	char *trackname = NULL;

	eof_log("eof_get_raw_MIDI_data() entered", 1);

	trackptr = malloc(sizeof(struct eof_MIDI_data_track));
	if(!trackptr)
	{	//Error allocating memory
		return NULL;
	}

	for(ctr = 0; ctr < 2; ctr++)
	{	//Two tracks will be parsed
		endreached = 0;	//Reset this condition
		track_pos = 0;	//Reset index to 0
		absdelta = reldelta = 0;	//Reset these counters
		lasteventtype = 0;
		if(ctr == 0)
		{	//First, track 0 will be read to create a tempo map
			curtrack = 0;
		}
		else
		{	//Then the specified track will be parsed
			curtrack = tracknum;
		}
		while(!endreached)
		{
//Parse the next MIDI event
			if(track_pos >= midiptr->track[curtrack].len)
			{	//If the end of the track data was reached unexpectedly
				eof_MIDI_empty_event_list(head);
				eof_MIDI_empty_tempo_list(tempohead);
				free(trackptr);
				return NULL;
			}
			runningstatus = 0;			//Running status is not considered in effect until it is found
			bytes_used = 0;
			delta = eof_parse_var_len(midiptr->track[curtrack].data, track_pos, &bytes_used);	//Read the variable length delta value
			track_pos += bytes_used;	//Keep track of our position within the MIDI
			absdelta += delta;			//Add it to the ongoing delta counter
			reldelta += delta;			//Add it to the ongoing relative (since last tempo change) counter
			event_pos = track_pos;		//Store the position of this event
			eventtype = midiptr->track[curtrack].data[track_pos];	//Read the MIDI event type
			if(eventtype < 0x80)		//All events have to have bit 7 set, if not, it's running status
			{
				if(((lasteventtype >> 4) == 0) || ((lasteventtype >> 4) == 0xF))
				{	//If the running status is not valid
					eof_MIDI_empty_event_list(head);
					eof_MIDI_empty_tempo_list(tempohead);
					free(trackptr);
					return NULL;
				}
				eventtype = lasteventtype;
				runningstatus = 1;
			}
			else
			{	//Not a running event
				track_pos++;	//Advance forward in the track
			}
			switch(eventtype >> 4)
			{
				case 0x8:	//Note off
				case 0x9:	//Note on
				case 0xA:	//Note Aftertouch
				case 0xB:	//Controller
				case 0xE:	//Pitch Bend
					size = 2;	//These MIDI events have two parameters
				break;

				case 0xC:	//Program Change
				case 0xD:	//Channel Aftertouch
					size = 1;	//These MIDI events only have one parameter instead of two
				break;

				case 0xF:	//Meta/Sysex Event
					if((eventtype & 0xF) == 0xF)
					{	//If it's a meta event
						meventtype = midiptr->track[curtrack].data[track_pos];	//Read the meta event type
						track_pos++;
						bytes_used = 0;
						length = eof_parse_var_len(midiptr->track[curtrack].data, track_pos, &bytes_used);	//Read the meta event length
						track_pos += bytes_used;
						size = length + bytes_used + 1;	//The size of this meta event is the size of the variable length value, the meta event data size and the meta event ID
						if(meventtype == 0x2F)
						{	//End of track
							endreached = 1;
						}
						else if((meventtype == 0x3) && (ctr > 0))
						{	//On the second pass, parse the track name
							trackname = malloc(length + 1);	//Allocate a buffer large enough to store the track name
							if(!trackname)
							{	//Error allocating memory
								eof_MIDI_empty_event_list(head);
								eof_MIDI_empty_tempo_list(tempohead);
								free(trackptr);
								return NULL;
							}
							memcpy(trackname, &midiptr->track[curtrack].data[track_pos], length);	//Read the track name into the buffer
							trackname[length] = '\0';	//Terminate the string
						}
						else if((meventtype == 0x51) && (ctr == 0))
						{	//On the first pass, process tempo changes to build the tempo map
							unsigned char mpqn_array[3];
							unsigned long mpqn;

							memcpy(mpqn_array, &midiptr->track[curtrack].data[track_pos], 3);
							mpqn = (mpqn_array[0]<<16) | (mpqn_array[1]<<8) | mpqn_array[2];	//Convert MPQN data to a usable value
							realtime += (double)reldelta / midiptr->divisions * ((double)60000.0 / currentbpm);	//Convert the relative delta time to real time and add it to the time counter
							currentbpm = (double)60000000.0 / mpqn;	//Obtain the BPM value of this tempo change

							tempoptr = malloc(sizeof(struct eof_MIDI_tempo_change));
							if(!tempoptr)
							{	//Error allocating memory
								eof_MIDI_empty_event_list(head);
								eof_MIDI_empty_tempo_list(tempohead);
								free(trackptr);
								return NULL;
							}
							tempoptr->absdelta = absdelta;
							tempoptr->realtime = realtime;
							tempoptr->BPM = currentbpm;
							tempoptr->next = NULL;
							if(tempohead == NULL)
							{	//If the list is empty
								tempohead = tempoptr;	//The new link is now the first link in the list
							}
							else if(tempotail != NULL)
							{	//If there is already a link at the end of the list
								tempotail->next = tempoptr;	//Point it forward to the new link
							}
							tempotail = tempoptr;	//The new link is the new tail of the list
							reldelta = 0;	//Reset the number of delta ticks since the last tempo change
						}
					}
					else if( ((eventtype & 0xF) == 0) || ((eventtype & 0xF) == 0x7) )
					{	//If it's a Sysex event
						bytes_used = 0;
						length = eof_parse_var_len(midiptr->track[curtrack].data, track_pos, &bytes_used);	//Read the meta event length
						size = length + bytes_used;	//The size of this Sysex event is the size of the variable length value and the Sysex event data size
					}
					else
					{	//Invalid event
						eof_MIDI_empty_event_list(head);
						eof_MIDI_empty_tempo_list(tempohead);
						free(trackptr);
						return NULL;
					}
				break;

				default:
					eof_MIDI_empty_event_list(head);
					eof_MIDI_empty_tempo_list(tempohead);
					free(trackptr);
					return NULL;	//Invalid event
				break;
			}
			if((eventtype >> 4) != 0xF)		//If this event wasn't a Meta/Sysex event
				lasteventtype=eventtype;	//Remember this in case Running Status is encountered

//Store the MIDI data into the linked list
			if(ctr != 0)
			{	//Don't store MIDI events on the first pass
				linkptr = malloc(sizeof(struct eof_MIDI_data_event));	//Allocate a new link, initialize it and insert it into the linked list
				dataptr = malloc(1 + size);	//Allocate enough memory to store the MIDI event type and the MIDI event data
				if(!linkptr || !dataptr)
				{	//Error allocating memory
					eof_MIDI_empty_event_list(head);
					eof_MIDI_empty_tempo_list(tempohead);
					free(trackptr);
					return NULL;
				}
				linkptr->size = size + 1 - runningstatus;	//Store the event's total size (including event ID, depending on whether or not running status is in effect)
				linkptr->data = dataptr;
				linkptr->next = NULL;
				if(head == NULL)
				{	//If the list is empty
					head = linkptr;	//The new link is now the first link in the list
				}
				else if(tail != NULL)
				{	//If there is already a link at the end of the list
					tail->next = linkptr;	//Point it forward to the new link
				}
				tail = linkptr;	//The new link is the new tail of the list
				memcpy(dataptr, &midiptr->track[curtrack].data[event_pos], 1 + size);	//Copy the MIDI event type and data
				linkptr->realtime = eof_MIDI_delta_to_realtime(tempohead, absdelta, midiptr->divisions);
#ifdef MIDI_DATA_IMPORT_DEBUG
				if((eventtype & 0xF) == 0xF)
				{	//If this was a meta event
					snprintf(eof_log_string, sizeof(eof_log_string), "\tStoring event:  Delta = %lu  Time = %fms  Event = 0x%X  Meta event = 0x%X",absdelta, linkptr->realtime, (eventtype >> 4), meventtype);
				}
				else
				{	//This was a normal MIDI event
					snprintf(eof_log_string, sizeof(eof_log_string), "\tStoring event:  Delta = %lu  Time = %fms  Event = 0x%X",absdelta, linkptr->realtime, (eventtype >> 4));
				}
				eof_log(eof_log_string, 1);
#endif
			}
			track_pos = event_pos + size + 1 - runningstatus;	//Advance beyond this event
		}//while(!endreached)
	}//Two tracks will be parsed

	trackptr->trackname = trackname;
	trackptr->events = head;
	trackptr->next = NULL;
	eof_MIDI_empty_tempo_list(tempohead);
#ifdef MIDI_DATA_IMPORT_DEBUG
	eof_log("\tStorage of MIDI data complete", 1);
#endif
	return trackptr;
}