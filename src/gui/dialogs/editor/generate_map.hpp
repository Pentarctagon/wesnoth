/*
	Copyright (C) 2008 - 2025
	by Mark de Wever <koraq@xs4all.nl>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#pragma once

#include "gui/dialogs/modal_dialog.hpp"
#include "utils/optional_fwd.hpp"

#include <cstdint>
#include <memory>

class map_generator;

namespace gui2::dialogs
{
class editor_generate_map : public modal_dialog
{
public:
	explicit editor_generate_map(std::vector<std::unique_ptr<map_generator>>& mg);

	std::vector<std::unique_ptr<map_generator>>& get_map_generators()
	{
		return map_generators_;
	}

	map_generator* get_selected_map_generator();

	void select_map_generator(map_generator* mg);

	utils::optional<uint32_t> get_seed();

private:
	virtual const std::string& window_id() const override;

	virtual void pre_show() override;

	/** Callback for generator list selection changes. */
	void do_generator_selected();

	/** Callback for the generator settings button. */
	void do_settings();

	/** Available map generators */
	std::vector<std::unique_ptr<map_generator>>& map_generators_;

	/** Last used map generator, must be in map_generators_ */
	map_generator* last_map_generator_;

	/** Current map generator index */
	int current_map_generator_;

	/** random seed integer input*/
	std::string random_seed_;
};

} // namespace gui2::dialogs
