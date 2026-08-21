#pragma once
#include "style.h"
#include "css_selector.h"

namespace litehtml
{
	class document_container;

	class css
	{
		css_selector::vector	m_selectors;
		bool					m_has_before_after;
	public:
		css()
		{
			m_has_before_after = false;
		}
		
		
		~css()
		{

		}

		const css_selector::vector& selectors() const
		{
			return m_selectors;
		}

		void clear()
		{
			m_selectors.clear();
			m_has_before_after = false;
		}

		void	parse_stylesheet(const tchar_t* str, const tchar_t* baseurl, document* doc, const media_query_list::ptr& media);
		void	sort_selectors();
		bool	has_before_after() const
		{
			return m_has_before_after;
		}
		static void	parse_css_url(const tstring& str, tstring& url);

	private:
		void	parse_atrule(const tstring& text, const tchar_t* baseurl, document* doc, const media_query_list::ptr& media);
		void	add_selector(css_selector::ptr selector);
		bool	parse_selectors(const tstring& txt, const litehtml::style::ptr& styles, const media_query_list::ptr& media);

	};

	inline void litehtml::css::add_selector( css_selector::ptr selector )
	{
		selector->m_order = (int) m_selectors.size();
		if(!m_has_before_after)
		{
			for(css_attribute_selector::vector::const_iterator it = selector->m_right.m_attrs.begin(); it != selector->m_right.m_attrs.end(); ++it)
			{
				if(it->condition == select_pseudo_element && (it->val == _t("before") || it->val == _t("after")))
				{
					m_has_before_after = true;
					break;
				}
			}
		}
		m_selectors.push_back(selector);
	}

}
