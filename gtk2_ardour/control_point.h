/*
 * Copyright (C) 2007-2011 David Robillard <d@drobilla.net>
 * Copyright (C) 2010-2012 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2012-2014 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2015-2017 Robin Gareus <robin@gareus.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <sys/types.h>
#include <ydk/gdkevents.h>

#include "ardour/automation_list.h"

#include "selectable.h"

class AutomationLine;
class ControlPoint;
class PointSelection;
class TimeAxisView;
class AutomationTimeAxisView;
class Selectable;
class Selection;

namespace ArdourCanvas {
	class Rectangle;
	class Diamond;
	class Item;
}

class ControlPoint : public Selectable
{
public:
	ControlPoint (AutomationLine& al);
	ControlPoint (const ControlPoint&, bool dummy_arg_to_force_special_copy_constructor);
	virtual ~ControlPoint ();

	enum ShapeType {
		Full,
		Start,
		End
	};

	void move_to (double x, double y);
	void move_to (double x, double y, ShapeType);
	void reset (double x, double y, ARDOUR::AutomationList::iterator, uint32_t, ShapeType);
	double get_x() const { return _x; }
	double get_y() const { return _y; }

	void hide ();
	void show ();
	bool visible () const;

	double size () const {
		return _size;
	}

	void set_size (double);
	void set_color ();

	bool     can_slide() const          { return _can_slide; }
	void     set_can_slide(bool yn)     { _can_slide = yn; }
	uint32_t view_index() const         { return _view_index; }
	void     set_view_index(uint32_t i) { _view_index = i; }

	ArdourCanvas::Item& item() const;

	/* used from ~EditorAutomationLine */
	void unset_item () { _item = 0 ; }

	ARDOUR::AutomationList::iterator model() const { return _model; }
	AutomationLine&              line()  const { return _line; }

	static PBD::Signal<void(ControlPoint *)> CatchDeletion;

private:
	ArdourCanvas::Rectangle *        _item;
	AutomationLine&              _line;
	ARDOUR::AutomationList::iterator _model;
	uint32_t                         _view_index;
	bool                             _can_slide;
	double                           _x;
	double                           _y;
	double                           _size;
	ShapeType                        _shape;

	virtual bool event_handler (GdkEvent*);

};



