#include "html.h"
#include "html_tag.h"
#include "document.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <vector>

/*
 * Flexbox (row axis) layout for litehtml.
 *
 * Implements the subset modern sites actually lean on: flex-direction row /
 * row-reverse (column degrades to block flow, which is equivalent for a
 * single column), flex-wrap, flex-basis / flex-grow / flex-shrink plus the
 * "flex" shorthand, order, justify-content and align-items. Items are laid
 * out as block-level boxes at their resolved main size, so nested flex
 * containers and ordinary block content both work unchanged.
 */

namespace litehtml
{
	struct flex_item
	{
		element::ptr	el;
		int				order;
		float			grow;
		float			shrink;
		int				base;		// outer main size before free-space distribution
		bool			has_main;	// base came from flex-basis / width
		int				ml, mr, mt, mb;
		int				main;		// final outer main size
		int				cross;		// final outer cross size
	};
}

using namespace litehtml;

static bool flex_flag(const tchar_t* val, const tchar_t* a, const tchar_t* b = 0)
{
	if(!val) return false;
	if(!t_strcasecmp(val, a)) return true;
	if(b && !t_strcasecmp(val, b)) return true;
	return false;
}

int litehtml::html_tag::render_flex( int x, int y, int max_width, bool second_pass /*= false*/ )
{
	int parent_width = max_width;

	calc_outlines(parent_width);

	m_pos.clear();
	m_pos.move_to(x, y);
	m_pos.x += content_margins_left();
	m_pos.y += content_margins_top();

	int ret_width = 0;
	int avail = max_width;
	bool width_auto = true;

	if(!m_css_width.is_predefined())
	{
		int w = calc_width(parent_width);
		if(m_box_sizing == box_sizing_border_box)
		{
			w -= m_padding.width() + m_borders.width();
		}
		ret_width = avail = w;
		width_auto = false;
	}
	else if(avail)
	{
		avail -= content_margins_left() + content_margins_right();
	}

	if(!m_css_max_width.is_predefined() && !second_pass)
	{
		int mw = get_document()->cvt_units(m_css_max_width, m_font_size, parent_width);
		if(m_box_sizing == box_sizing_border_box)
		{
			mw -= m_padding.left + m_borders.left + m_padding.right + m_borders.right;
		}
		if(avail > mw)
		{
			avail = mw;
		}
	}
	if(avail < 0) avail = 0;

	const tchar_t* dir_s = get_style_property(_t("flex-direction"), false, _t("row"));
	if(flex_flag(dir_s, "column", "column-reverse"))
	{
		// a single flex column is block flow for the subset we support
		return render_box(x, y, max_width, second_pass);
	}
	bool row_reverse = flex_flag(dir_s, "row-reverse");

	const tchar_t* wrap_s	= get_style_property(_t("flex-wrap"), false, _t("nowrap"));
	bool do_wrap			= flex_flag(wrap_s, "wrap", "wrap-reverse");
	const tchar_t* jc_s		= get_style_property(_t("justify-content"), false, _t("flex-start"));
	const tchar_t* ai_s		= get_style_property(_t("align-items"), false, _t("stretch"));

	std::vector<flex_item> items;
	for(auto& el : m_children)
	{
		if(!el || !el->is_visible()) continue;
		element_position ep = el->get_element_position();
		if(ep == element_position_absolute || ep == element_position_fixed) continue;
		if(el->is_white_space()) continue;

		/* CSS blockifies flex items: inline-level boxes become block-level */
		switch(el->get_display())
		{
		case display_inline:		el->set_display(display_block);		break;
		case display_inline_block:	el->set_display(display_block);		break;
		case display_inline_table:	el->set_display(display_table);		break;
		case display_inline_flex:	el->set_display(display_flex);		break;
		case display_inline_grid:	el->set_display(display_grid);		break;
		default:						break;
		}

		flex_item it;
		it.el		= el;
		it.grow		= 0;
		it.shrink	= 1;
		it.base		= 0;
		it.has_main	= false;
		it.main		= 0;
		it.cross	= 0;
		it.ml = el->margin_left();
		it.mr = el->margin_right();
		it.mt = el->margin_top();
		it.mb = el->margin_bottom();

		const tchar_t* v = el->get_style_property(_t("order"), false, _t("0"));
		it.order = v ? atoi(v) : 0;

		const tchar_t* sh = el->get_style_property(_t("flex"), false, 0);
		if(sh)
		{
			float g = 0, s = 1, b = -1;
			int n = sscanf(sh, "%f %f %f", &g, &s, &b);
			if(n >= 1) it.grow = g;
			if(n >= 2) it.shrink = s;
			if(n >= 3 && b >= 0)
			{
				it.base = (int)b + it.ml + it.mr;
				it.has_main = true;
			}
		}
		v = el->get_style_property(_t("flex-grow"), false, 0);
		if(v) it.grow = (float)atof(v);
		v = el->get_style_property(_t("flex-shrink"), false, 0);
		if(v) it.shrink = (float)atof(v);

		css_length cw = el->get_css_width();
		/* css_units_none means "no explicit width" (auto / unset). A default-constructed
		 * css_length has is_predefined()==false yet units none and value 0 — that is what
		 * text nodes and other never-width-styled items report. Treating it as an explicit
		 * 0 width collapsed them (e.g. the text inside a `display:flex` nav-link measured
		 * base=0 and was never rendered). Require a real unit before using width as base. */
		if(!cw.is_predefined() && cw.units() != css_units_none)
		{
			it.base = get_document()->cvt_units(cw, el->get_font_size(), avail) + it.ml + it.mr;
			it.has_main = true;
		}
		v = el->get_style_property(_t("flex-basis"), false, 0);
		if(v && !flex_flag(v, "auto", "content"))
		{
			css_length bl;
			bl.fromString(v);
			if(!bl.is_predefined())
			{
				it.base = get_document()->cvt_units(bl, el->get_font_size(), avail) + it.ml + it.mr;
				it.has_main = true;
			}
		}
		items.push_back(it);
	}

	if(items.empty())
	{
		m_pos.width = avail;
		m_pos.height = 0;
		calc_auto_margins(parent_width);
		return ret_width + content_margins_left() + content_margins_right();
	}

	std::stable_sort(items.begin(), items.end(),
			[](const flex_item& a, const flex_item& b){ return a.order < b.order; });

	// measure the preferred main size of flexible items
	for(auto& it : items)
	{
		if(!it.has_main)
		{
			if(it.el->get_display() == display_inline_text)
			{
				/* text nodes have no render() override (base element::render returns 0),
				 * so measure them via get_content_size exactly as place_element does;
				 * otherwise a bare text flex item collapses to base 0 */
				litehtml::size sz;
				it.el->get_content_size(sz, avail);
				it.base = sz.width + it.ml + it.mr;
			}
			else
			{
				it.base = it.el->render(0, 0, avail, second_pass);
			}
		}
		if(it.base < it.ml + it.mr) it.base = it.ml + it.mr;
	}

	// break into flex lines
	std::vector<std::vector<int> > lines;
	{
		std::vector<int> cur;
		int used = 0;
		for(size_t i = 0; i < items.size(); i++)
		{
			if(do_wrap && !cur.empty() && used + items[i].base > avail)
			{
				lines.push_back(cur);
				cur.clear();
				used = 0;
			}
			cur.push_back((int)i);
			used += items[i].base;
		}
		if(!cur.empty()) lines.push_back(cur);
	}

	int bottom = 0;
	int used_width = 0;

	for(size_t li = 0; li < lines.size(); li++)
	{
		std::vector<int>& line = lines[li];

		int sum = 0;
		float total_grow = 0, total_shrink = 0;
		for(size_t i = 0; i < line.size(); i++)
		{
			flex_item& it = items[line[i]];
			sum += it.base;
			total_grow += it.grow;
			total_shrink += it.shrink * (float)it.base;
		}

		int free = avail - sum;
		if(free > 0 && total_grow > 0)
		{
			for(size_t i = 0; i < line.size(); i++)
			{
				flex_item& it = items[line[i]];
				it.main = it.base + (int)((float)free * it.grow / total_grow);
			}
			free = 0;
		}
		else if(free < 0 && total_shrink > 0)
		{
			for(size_t i = 0; i < line.size(); i++)
			{
				flex_item& it = items[line[i]];
				int sh = (int)((float)(-free) * it.shrink * (float)it.base / total_shrink);
				it.main = it.base - sh;
				if(it.main < it.ml + it.mr) it.main = it.ml + it.mr;
			}
			free = 0;
		}
		else
		{
			for(size_t i = 0; i < line.size(); i++)
			{
				items[line[i]].main = items[line[i]].base;
			}
		}
		if(free < 0) free = 0;

		// lay the items out at their final main size to learn cross sizes
		int line_cross = 0;
		for(size_t i = 0; i < line.size(); i++)
		{
			flex_item& it = items[line[i]];
			if(it.el->get_display() == display_inline_text)
			{
				/* text has no render(); size it here so its height feeds line_cross */
				litehtml::size sz;
				it.el->get_content_size(sz, it.main);
				it.el->m_pos = sz;
				it.cross = sz.height + it.mt + it.mb;
			}
			else
			{
				it.el->render(0, 0, it.main, second_pass);
				it.cross = it.el->get_position().height + it.mt + it.mb;
			}
			if(it.cross > line_cross) line_cross = it.cross;
		}

		// main-axis packing of the leftover space
		int lead = 0, gap = 0;
		if(flex_flag(jc_s, "center"))
		{
			lead = free / 2;
		}
		else if(flex_flag(jc_s, "flex-end", "end"))
		{
			lead = free;
		}
		else if(flex_flag(jc_s, "space-between") && line.size() > 1)
		{
			gap = free / (int)(line.size() - 1);
		}
		else if(flex_flag(jc_s, "space-around") && !line.empty())
		{
			gap = free / (int)line.size();
			lead = gap / 2;
		}

		std::vector<int> xs(line.size());
		{
			int xoff = lead;
			for(size_t i = 0; i < line.size(); i++)
			{
				xs[i] = xoff;
				xoff += items[line[i]].main + gap;
			}
			if(row_reverse)
			{
				int total = xoff - gap;
				for(size_t i = 0; i < line.size(); i++)
				{
					xs[i] = avail - total + (total - xs[i] - items[line[i]].main);
				}
			}
		}

		for(size_t i = 0; i < line.size(); i++)
		{
			flex_item& it = items[line[i]];
			int iy = 0;
			bool is_text = (it.el->get_display() == display_inline_text);
			css_length ch = it.el->get_css_height();
			if(!is_text && flex_flag(ai_s, "stretch", "normal") && ch.is_predefined())
			{
				iy = 0;
				it.el->get_position().height = line_cross - it.mt - it.mb;
				it.cross = line_cross;
			}
			else if(flex_flag(ai_s, "center"))
			{
				iy = (line_cross - it.cross) / 2;
			}
			else if(flex_flag(ai_s, "flex-end", "end"))
			{
				iy = line_cross - it.cross;
			}
			if(is_text)
			{
				/* position the text box directly; render() is a no-op for text */
				it.el->m_pos.x = m_pos.x + xs[i];
				it.el->m_pos.y = m_pos.y + bottom + iy;
			}
			else
			{
				it.el->render(m_pos.x + xs[i], m_pos.y + bottom + iy, it.main, second_pass);
			}
		}

		int line_used = lead;
		for(size_t i = 0; i < line.size(); i++)
		{
			line_used += items[line[i]].main;
			if(i + 1 < line.size()) line_used += gap;
		}
		if(line_used > used_width) used_width = line_used;

		bottom += line_cross;
	}

	/* width_auto: size to the container (block) or the used content (inline-flex).
	 * explicit width: avail already holds the resolved content width (calc_width
	 * minus padding/borders for border-box, capped by max-width). Assigning it here
	 * is essential — m_pos.clear() zeroed m_pos.width and the explicit branch above
	 * only set `avail`, so without this an explicit-width flex container renders 0
	 * wide (e.g. a nested `.global-nav ul { width:100% }`). */
	m_pos.width = width_auto ? (m_display == display_inline_flex ? std::min(avail, used_width) : avail) : avail;
	m_pos.height = bottom;
	calc_auto_margins(parent_width);

	int min_height = 0;
	if(!m_css_min_height.is_predefined())
	{
		min_height = (int)m_css_min_height.val();
	}
	if(min_height > m_pos.height)
	{
		m_pos.height = min_height;
	}

	m_pos.move_to(x, y);
	m_pos.x += content_margins_left();
	m_pos.y += content_margins_top();

	if(used_width > ret_width) ret_width = used_width;
	ret_width += content_margins_left() + content_margins_right();
	return ret_width;
}
