#pragma once

#include <stdint.h>
#include <stddef.h>

namespace le_char_tree {

// a tree structure of chars,
// created from a sorted list of strings
//

class node_t {

	// --------------------------------------------------

	char    c            = 0;
	node_t* next_sibling = nullptr;
	node_t* first_child  = nullptr;

	// --------------------------------------------------

  public:
	~node_t() {
		delete ( next_sibling );
		delete ( first_child );
	}

	// --------------------------------------------------

	node_t* add_child( char c_ ) {
		auto n          = new node_t;
		n->c            = c_;
		n->first_child  = nullptr;
		n->next_sibling = nullptr;

		node_t* child = first_child;

		if ( nullptr == first_child ) {
			first_child = n;
		} else {
			while ( child->next_sibling ) {
				child = child->next_sibling;
			}
			child->next_sibling = n;
		}

		return n;
	}

	// --------------------------------------------------

	inline node_t* get_next_sibling() {
		return next_sibling;
	}

	// --------------------------------------------------

	inline node_t* get_first_child() {
		return first_child;
	}

	// --------------------------------------------------

	inline char get_value() {
		return c;
	}

	// --------------------------------------------------

	void add_children( size_t depth, char const** strs_begin, char const** const strs_end ) {

		// everything that has char c belongs to the current child

		if ( strs_begin == strs_end ) {
			return;
		}

		{
			size_t      str_begin_length = 0;
			char const* s                = *strs_begin;
			while ( *s++ ) {
				str_begin_length++;
			}
			if ( str_begin_length == depth ) {
				strs_begin++;
			}
			if ( strs_begin == strs_end ) {
				return;
			}
		}

		char c = ( *strs_begin )[ depth ];

		auto str = strs_begin;

		while ( true ) {

			if ( str == strs_end || ( *str )[ depth ] != c ) {

				if ( strs_begin != str ) {
					// printf( " %s%c\n", std::string( depth, ' ' ).c_str(), c );
					// add child to current node
					node_t* child = this->add_child( c );
					child->add_children( depth + 1, strs_begin, str );
				}

				if ( str == strs_end ) {
					break;
				} else {
					strs_begin = str;
					c          = ( *strs_begin )[ depth ];
				}
			}
			str++;
		}
	}

	// --------------------------------------------------

	// If suggestion can be made, places up to `suggestion_len` chars into suggestion,
	// updates suggestion_len to length of full suggestion (WITHOUT zero terminator)
	bool get_suggestion_at( uint32_t first_sibling_index, size_t* suggestion_len, char* suggestion ) {

		int num_chars_available = *suggestion_len;
		*suggestion_len         = 0;

		node_t* node = this->get_first_child();

		for ( uint32_t i = 0; i != first_sibling_index; i++ ) {
			if ( node && node->get_next_sibling() ) {
				node = node->get_next_sibling();
			} else {
				*suggestion_len = 0;
				return false;
			}
		}

		while ( node ) {
			if ( num_chars_available > 0 ) {
				suggestion[ *suggestion_len ] = node->get_value();
				num_chars_available--;
			}
			( *suggestion_len )++;
			node = node->get_first_child();
			if ( node && node->get_next_sibling() ) {
				break;
			}
		}
		return ( *suggestion_len ) > 0;
	}

	// --------------------------------------------------

	size_t count_siblings() {
		size_t count = 0;

		auto node = this;

		// C11: §6.5.16 An assignment expression has
		// the value of the left operand after the assignment
		while ( ( node = node->get_next_sibling() ) ) {
			count++;
		}

		return count;
	}

	// --------------------------------------------------

	// Returns found node or `this` if not found,
	// and upates found_len to count of found characters
	node_t* find_word( char const* needle, size_t* found_len ) {

		// traverse node until we cannot find needle anymore
		// we must take the first child of the root node
		node_t* previous_node = this;

		// ----------| Invariant: root node exists

		node_t*     node = this->get_first_child();
		char const* np   = needle; // needle point (the first char of the needle)

		while ( *np && node ) {
			if ( node->get_value() == *np ) {
				// found the correct node
				// printf( "found: %c\n", *np );
				np++;
				( *found_len )++;
				previous_node = node;
				node          = node->get_first_child();
			} else {
				node = node->get_next_sibling();
			}
		}

		return previous_node;
	};

	// --------------------------------------------------

	typedef void ( *visit_cb )( char c, void* user_data, size_t depth );

	void visit( visit_cb cb, void* user_data = nullptr, size_t depth = 0 ) {

		node_t* child = this->get_first_child();

		// If there is more than sibling, we must visit all children
		while ( child ) {
			cb( child->get_value(), user_data, depth );
			child->visit( cb, user_data, depth + 1 );
			child = child->get_next_sibling();
		}
	}
};

} // namespace le_char_tree
