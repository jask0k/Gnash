// 
//   Copyright (C) 2005, 2006, 2007, 2008, 2009 Free Software Foundation, Inc.
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#ifndef GNASH_STATIC_TEXT_H
#define GNASH_STATIC_TEXT_H

#include "smart_ptr.h" // GNASH_USE_GC
#include "DisplayObject.h" // for inheritance
#include "DisplayObject.h"
#include "swf/DefineTextTag.h"

#include <boost/dynamic_bitset.hpp>
#include <cassert>

// Forward declarations
namespace gnash {
    class character_def;
    namespace SWF {
        class TextRecord;
    }
}

namespace gnash {

/// Static text fields, SWF-defined with read-only text.
//
/// StaticText objects hold mutable selection and color information.
class StaticText : public DisplayObject
{
public:

	StaticText(SWF::DefineTextTag* def, DisplayObject* parent, int id)
		:
        DisplayObject(parent, id),
        _def(def)
	{
        assert(_def);
	}

    /// Return a pointer to this if our definition contains any static text.
    //
    /// This is non-const because a TextSnapshot needs to add selection and
    /// color information to this StaticText. It also resets selection.
    //
    /// @param to       A vector of pointers to TextRecords containing text.
    /// @param numChars The total number of DisplayObjects in all TextRecords is
    ///                 written to this variable.
    /// Note: This function always removes any existing selection and resizes
    /// the bitset to the number of DisplayObjects in all TextRecords.
    virtual StaticText* getStaticText(std::vector<const SWF::TextRecord*>& to,
            size_t& numChars);

	virtual void display();

    void setSelected(size_t pos, bool selected) {
        _selectedText.set(pos, selected);
    }

    /// Return a bitset showing which DisplayObjects (by index) are selected.
    //
    /// Note: mutable information is meaningless until the StaticText is
    /// queried with getStaticText(). This is because under normal
    /// circumstances there is no need for it.
    /// Note also: the size() member of boost::dynamic_bitset returns 0 before
    /// getStaticText() is called; afterwards it is equivalent to the
    /// number of DisplayObjects in the StaticText's definition.
    const boost::dynamic_bitset<>& getSelected() const {
        return _selectedText;
    }

    void setSelectionColor(boost::uint32_t color);

    virtual rect getBounds() const {
        return _def->get_bound();
    }

    virtual bool pointInShape(boost::int32_t  x, boost::int32_t  y) const;

protected:

#ifdef GNASH_USE_GC
	/// Mark reachable resources (for the GC)
	void markReachableResources() const
	{
		assert(isReachable());
        _def->setReachable();
		markDisplayObjectReachable();
	}
#endif

private:

    const boost::intrusive_ptr<SWF::DefineTextTag> _def;

    /// A bitmask indicating which static text DisplayObjects are selected
    //
    /// This is only present for static text fields, and only after
    /// a TextSnapshot has queried the DisplayObject for text.
    boost::dynamic_bitset<> _selectedText;

    /// The color of the background for selected DisplayObjects.
    //
    /// This is alawys opaque.
    rgba _selectionColor;

};


}	// end namespace gnash


#endif 


// Local Variables:
// mode: C++
// indent-tabs-mode: t
// End:
