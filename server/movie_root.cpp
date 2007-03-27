// 
//   Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
// 
//

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_PTHREADS
#include <pthread.h>
#endif

#include "movie_root.h"
#include "log.h"
#include "sprite_instance.h"
#include "movie_instance.h" // for implicit upcast to sprite_instance
#include "render.h"
#include "VM.h"
#include "tu_random.h"
#include "ExecutableCode.h"

#include <iostream>
#include <string>
#include <typeinfo>
#include <cassert>
#include <boost/ptr_container/ptr_list.hpp>

using namespace std;

namespace {
gnash::LogFile& dbglogfile = gnash::LogFile::getDefaultInstance();
}

namespace gnash
{

inline bool
movie_root::testInvariant() const
{
	// TODO: fill this function !
	assert(_movie.get());

	return true;
}


movie_root::movie_root()
	:
	m_viewport_x0(0),
	m_viewport_y0(0),
	m_viewport_width(1),
	m_viewport_height(1),
	m_pixel_scale(1.0f),
	m_background_color(0, 0, 0, 255),
	m_timer(0.0f),
	m_mouse_x(0),
	m_mouse_y(0),
	m_mouse_buttons(0),
	m_userdata(NULL),
	m_on_event_xmlsocket_ondata_called(false),
	m_on_event_xmlsocket_onxml_called(false),
	m_on_event_load_progress_called(false),
	m_active_input_text(NULL),
	m_time_remainder(0.0f),
	m_drag_state()
{
}

movie_root::~movie_root()
{
	assert(testInvariant());
}

void
movie_root::setRootMovie(movie_instance* movie)
{
	assert(movie != NULL);
	_movie = movie;

	_movie->set_invalidated();
	
	set_display_viewport(0, 0,
		(int) _movie->get_movie_definition()->get_width_pixels(),
		(int) _movie->get_movie_definition()->get_height_pixels());

	assert(testInvariant());
}

		
void
movie_root::set_display_viewport(int x0, int y0, int w, int h)
{
	assert(testInvariant());

    m_viewport_x0 = x0;
    m_viewport_y0 = y0;
    m_viewport_width = w;
    m_viewport_height = h;

    // should we cache this ? it's immutable after all !
    const rect& frame_size = _movie->get_frame_size();

    // Recompute pixel scale.

    float	scale_x = m_viewport_width / TWIPS_TO_PIXELS(frame_size.width());
    float	scale_y = m_viewport_height / TWIPS_TO_PIXELS(frame_size.height());
    m_pixel_scale = fmax(scale_x, scale_y);

	assert(testInvariant());
}

bool
movie_root::notify_mouse_moved(int x, int y)
{
	assert(testInvariant());

    m_mouse_x = x;
    m_mouse_y = y;
    notify_mouse_listeners(event_id(event_id::MOUSE_MOVE));
    return fire_mouse_event();

}

bool
movie_root::notify_mouse_clicked(bool mouse_pressed, int button_mask)
{
	assert(testInvariant());

	//log_msg("Mouse click notification");
	if (mouse_pressed)
	{
		m_mouse_buttons |= button_mask;
		notify_mouse_listeners(event_id(event_id::MOUSE_DOWN));
	}
	else
	{
		m_mouse_buttons &= ~button_mask;
		notify_mouse_listeners(event_id(event_id::MOUSE_UP));
	}

	return fire_mouse_event();
}

void
movie_root::notify_mouse_state(int x, int y, int buttons)
{
	assert(testInvariant());

    m_mouse_x = x;
    m_mouse_y = y;
    m_mouse_buttons = buttons;
    fire_mouse_event();

	assert(testInvariant());
}

// Return wheter any action triggered by this event requires display redraw.
// See page about events_handling (in movie_interface.h)
//
/// TODO: make this code more readable !
bool
generate_mouse_button_events(mouse_button_state* ms)
{
	boost::intrusive_ptr<character> active_entity = ms->m_active_entity;
	boost::intrusive_ptr<character> topmost_entity = ms->m_topmost_entity;

	// Did this event trigger any action that needs redisplay ?
	bool need_redisplay = false;

	if (ms->m_mouse_button_state_last == mouse_button_state::DOWN)
	{
		// Mouse button was down.

		// TODO: Handle trackAsMenu dragOver

		// Handle onDragOut, onDragOver
		if (ms->m_mouse_inside_entity_last == false)
		{
			if (topmost_entity == active_entity)
			{
				// onDragOver
				if (active_entity != NULL)
				{
					active_entity->on_button_event(event_id::DRAG_OVER);
					// TODO: have on_button_event return
					//       wheter the action must trigger
					//       a redraw.
					need_redisplay=true;
				}
				ms->m_mouse_inside_entity_last = true;
			}
		}
		else
		{
			// mouse_inside_entity_last == true
			if (topmost_entity != active_entity)
			{
				// onDragOut
				if (active_entity != NULL)
				{
					assert(active_entity->get_ref_count() > 1); // we are NOT the only object holder !
					active_entity->on_button_event(event_id::DRAG_OUT);
					// TODO: have on_button_event return
					//       wheter the action must trigger
					//       a redraw.
					need_redisplay=true;
				}
				ms->m_mouse_inside_entity_last = false;
			}
		}

		// Handle onRelease, onReleaseOutside
		if (ms->m_mouse_button_state_current == mouse_button_state::UP)
		{
			// Mouse button just went up.
			ms->m_mouse_button_state_last = mouse_button_state::UP;

			if (active_entity != NULL)
			{
				if (ms->m_mouse_inside_entity_last)
				{
					// onRelease
					active_entity->on_button_event(event_id::RELEASE);
					// TODO: have on_button_event return
					//       wheter the action must trigger
					//       a redraw.
					need_redisplay=true;
				}
				else
				{
					// TODO: Handle trackAsMenu 

					// onReleaseOutside
					active_entity->on_button_event(event_id::RELEASE_OUTSIDE);
					// TODO: have on_button_event return
					//       wheter the action must trigger
					//       a redraw.
					need_redisplay=true;
				}
			}
		}
	}

	if ( ms->m_mouse_button_state_last == mouse_button_state::UP )
	{
		// Mouse button was up.

		// New active entity is whatever is below the mouse right now.
		if (topmost_entity != active_entity)
		{
			// onRollOut
			if (active_entity != NULL)
			{
				active_entity->on_button_event(event_id::ROLL_OUT);
				// TODO: have on_button_event return
				//       wheter the action must trigger
				//       a redraw.
				need_redisplay=true;
			}

			active_entity = topmost_entity;

			// onRollOver
			if (active_entity != NULL)
			{
				active_entity->on_button_event(event_id::ROLL_OVER);
				// TODO: have on_button_event return
				//       wheter the action must trigger
				//       a redraw.
				need_redisplay=true;
			}

			ms->m_mouse_inside_entity_last = true;
		}

		// mouse button press
		if (ms->m_mouse_button_state_current == mouse_button_state::DOWN )
		{
			// onPress

			// set/kill focus for current root
			movie_root& mroot = VM::get().getRoot();
			character* current_active_entity = mroot.get_active_entity();

			// It's another entity ?
			if (current_active_entity != active_entity.get())
			{
				// First to clean focus
				if (current_active_entity != NULL)
				{
					current_active_entity->on_event(event_id::KILLFOCUS);
					// TODO: have on_button_event return
					//       wheter the action must trigger
					//       a redraw.
					need_redisplay=true;
					mroot.set_active_entity(NULL);
				}

				// Then to set focus
				if (active_entity != NULL)
				{
					if (active_entity->on_event(event_id::SETFOCUS))
					{
						mroot.set_active_entity(active_entity.get());
					}
				}
			}

			if (active_entity != NULL)
			{
				active_entity->on_button_event(event_id::PRESS);
				// TODO: have on_button_event return
				//       wheter the action must trigger
				//       a redraw.
				need_redisplay=true;
			}
			ms->m_mouse_inside_entity_last = true;
			ms->m_mouse_button_state_last = mouse_button_state::DOWN;
		}
	}

	// Write the (possibly modified) boost::intrusive_ptr copies back
	// into the state struct.
	ms->m_active_entity = active_entity;
	ms->m_topmost_entity = topmost_entity;

	//if ( ! need_redisplay ) log_msg("Hurray, an event didnt' trigger redisplay!");
	return need_redisplay;
}


bool
movie_root::fire_mouse_event()
{
//	GNASH_REPORT_FUNCTION;

	assert(testInvariant());

    // Generate a mouse event
    m_mouse_button_state.m_topmost_entity =
        _movie->get_topmost_mouse_entity(PIXELS_TO_TWIPS(m_mouse_x), PIXELS_TO_TWIPS(m_mouse_y));
    m_mouse_button_state.m_mouse_button_state_current = (m_mouse_buttons & 1);

    return generate_mouse_button_events(&m_mouse_button_state);

}

void
movie_root::get_mouse_state(int& x, int& y, int& buttons)
{
//	    GNASH_REPORT_FUNCTION;

//             dbglogfile << "X is: " << m_mouse_x << " Y is: " << m_mouse_y
//                        << " Button is: "
//                        << m_mouse_buttons << endl;

	assert(testInvariant());

	x = m_mouse_x;
	y = m_mouse_y;
	buttons = m_mouse_buttons;

	assert(testInvariant());
}

void
movie_root::get_drag_state(drag_state& st)
{
	assert(testInvariant());

	st = m_drag_state;

	assert(testInvariant());
}

void
movie_root::set_drag_state(const drag_state& st)
{
	m_drag_state = st;
	assert(testInvariant());
}

#if 0 // see comments in movie_root.h
void
movie_root::get_url(const char *url)
{
    GNASH_REPORT_FUNCTION;

	// nobody should use this function
	assert(0);
    
    string command = "mozilla -remote \"openurl";
    command += url;
    command += ")\"";
    dbglogfile << "Launching URL... " << command << endl;
    system(command.c_str());
}
#endif

unsigned int
movie_root::add_interval_timer(const Timer& timer)
{
	assert(testInvariant());
			
	int id = _intervalTimers.size()+1;

	// TODO: find first NULL element in vector for reuse ?
	_intervalTimers.push_back(timer);
	return id;
}
	
bool
movie_root::clear_interval_timer(unsigned int x)
{
	if ( ! x || x > _intervalTimers.size() ) return false;

	Timer& timer = _intervalTimers[x-1];

	// will make sure next expire() will always return false!
	timer.clearInterval();

	assert(testInvariant());

	return true;
}
	
void
movie_root::advance(float delta_time)
{
	// GNASH_REPORT_FUNCTION;

	// TODO: wrap this in a executeTimers() method 
	for (TimerList::iterator it=_intervalTimers.begin(),
			itEnd=_intervalTimers.end();
			it != itEnd;
			++it)
	{
		Timer& timer = *it;
		if ( timer.expired() )
		{
			// log_msg("FIXME: Interval Timer Expired!\n");
			//_movie->on_event_interval_timer();
			timer();
		}
	}

	// random should go continuously that:
	// 1. after restart of the player the situation has not repeated
	// 2. by different machines the random gave different numbers
	tu_random::next_random();
			
#ifdef GNASH_DEBUG
	size_t totframes = _movie->get_frame_count();
	size_t prevframe = _movie->get_current_frame();
#endif

	// Keep root sprite alive during actions execution.
	//
	// This is *very* important, as actions in the movie itself
	// could get rid of it. A simple example:
	//
	// 	_root.loadMovie(other);
	//
	boost::intrusive_ptr<sprite_instance> keepMovieAlive(_movie.get());

	_movie->advance(delta_time);

	processActionQueue();

#ifdef GNASH_DEBUG
	size_t curframe = _movie->get_current_frame();

	log_msg("movie_root::advance advanced top-level movie from "
			SIZET_FMT "/" SIZET_FMT
			" to " SIZET_FMT "/" SIZET_FMT
			" (_movie is %s%s)",
			prevframe, totframes, curframe, totframes,
			typeid(*_movie).name(),
			_movie->get_play_state() == sprite_instance::STOP ? " - now in STOP mode" : "");
#endif

	assert(testInvariant());
}


void
movie_root::display()
{
//	GNASH_REPORT_FUNCTION;

	assert(testInvariant());

	_movie->clear_invalidated();

//  	    GNASH_REPORT_FUNCTION;
	if (_movie->get_visible() == false)
        {
            // Don't display.
            return;
        }

	// should we cache this ? it's immutable after all !
	const rect& frame_size = _movie->get_frame_size();

	// null frame size ? don't display !
	if ( frame_size.is_null() ) return;

	render::begin_display(
		m_background_color,
		m_viewport_x0, m_viewport_y0,
		m_viewport_width, m_viewport_height,
		frame_size.get_x_min(), frame_size.get_x_max(),
		frame_size.get_y_min(), frame_size.get_y_max());

	_movie->display();

	render::end_display();
}


const char*
movie_root::call_method(const char* method_name,
		const char* method_arg_fmt, ...)
{
	assert(testInvariant());

	va_list	args;
	va_start(args, method_arg_fmt);
	const char* result = _movie->call_method_args(method_name,
		method_arg_fmt, args);
	va_end(args);

	return result;
}

const
char* movie_root::call_method_args(const char* method_name,
		const char* method_arg_fmt, va_list args)
{
	assert(testInvariant());
	return _movie->call_method_args(method_name, method_arg_fmt, args);
}

void movie_root::notify_keypress_listeners(key::code k)
{
	for (ListenerSet::iterator iter = m_keypress_listeners.begin();
			 iter != m_keypress_listeners.end(); ++iter)
	{
		// sprite, button & input_edit_text characters
		// TODO: invoke functions on non-characters !
		character* ch = dynamic_cast<character*>(iter->get());
		if ( ch )
		{
			ch->on_event(event_id(event_id::KEY_PRESS, k));
		}
	}

	assert(testInvariant());
}

void movie_root::add_keypress_listener(as_object* listener)
{
	m_keypress_listeners.insert(listener);
	assert(testInvariant());
}

void movie_root::remove_keypress_listener(as_object* listener)
{
	m_keypress_listeners.erase(listener);
	assert(testInvariant());
}

void movie_root::add_mouse_listener(as_object* listener)
{
	m_mouse_listeners.insert(listener);
	assert(testInvariant());
}

void movie_root::remove_mouse_listener(as_object* listener)
{
	m_mouse_listeners.erase(listener);
	assert(testInvariant());
}

void movie_root::notify_mouse_listeners(const event_id& event)
{
	//log_msg("Notifying " SIZET_FMT " listeners about %s",
	//		m_mouse_listeners.size(), event.get_function_name().c_str());

	for (ListenerSet::iterator iter = m_mouse_listeners.begin();
			iter != m_mouse_listeners.end(); ++iter)
	{
		// sprite, button & input_edit_text characters
		// TODO: invoke functions on non-characters !
		character* ch = dynamic_cast<character*>(iter->get()); 
		if ( ch )
		{
			ch->on_event(event);
		}
	}

	assert(testInvariant());
}

character*
movie_root::get_active_entity()
{
	assert(testInvariant());
	return m_active_input_text;
}

void
movie_root::set_active_entity(character* ch)
{
	m_active_input_text = ch;
	assert(testInvariant());
}

bool
movie_root::isMouseOverActiveEntity() const
{
	assert(testInvariant());

	boost::intrusive_ptr<character> entity ( m_mouse_button_state.m_active_entity );
	if ( ! entity.get() ) return false;

#if 0 // debugging...
	log_msg("The active entity under the pointer is a %s",
		typeid(*entity).name());
#endif

	return true;
}

void
movie_root::add_invalidated_bounds(InvalidatedRanges& ranges, bool force)
{
	_movie->add_invalidated_bounds(ranges, force);
}

void
movie_root::processActionQueue()
{

#ifdef GNASH_DEBUG
	static unsigned calls=0;
	++calls;
	bool actionsToProcess = !_actionQueue.empty();
	if ( actionsToProcess ) log_msg(" Processing action queue (call %u)", calls);
#endif

	// _actionQueue may be changed due to actions (appended-to)
	// this loop might be optimized by using an iterator
	// and a final call to .clear() 
	while ( ! _actionQueue.empty() )
	{
		ExecutableCode& code = _actionQueue.front();
		code.execute();
		_actionQueue.pop_front(); 
	}

	assert(_actionQueue.empty());

#ifdef GNASH_DEBUG
	if ( actionsToProcess ) log_msg(" Done processing action queue (call %u)", calls);
#endif
}

void
movie_root::pushAction(const action_buffer& buf, boost::intrusive_ptr<sprite_instance> target)
{
#ifdef GNASH_DEBUG
	log_msg("Pushed action buffer for target %s", target->getTargetPath().c_str());
#endif
	_actionQueue.push_back(new GlobalCode(buf, target));
}

void
movie_root::pushAction(boost::intrusive_ptr<as_function> func, boost::intrusive_ptr<sprite_instance> target)
{
#ifdef GNASH_DEBUG
	log_msg("Pushed function (event hanlder?) with target %s", target->getTargetPath().c_str());
#endif
	_actionQueue.push_back(new FunctionCode(func, target));
}

} // namespace gnash

