/*
	Copyright (C) 2003 - 2025
	by David White <dave@whitevine.net>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

/**
 * @file
 * During a game, show map & info-panels at top+right.
 */

#include "game_display.hpp"

#include <utility>


#include "cursor.hpp"
#include "display_chat_manager.hpp"
#include "fake_unit_manager.hpp"
#include "floating_label.hpp"
#include "game_board.hpp"
#include "preferences/preferences.hpp"
#include "log.hpp"
#include "map/map.hpp"
#include "font/standard_colors.hpp"
#include "reports.hpp"
#include "resources.hpp"
#include "tod_manager.hpp"
#include "color.hpp"
#include "synced_context.hpp"
#include "units/unit.hpp"
#include "units/drawer.hpp"
#include "utils/general.hpp"
#include "whiteboard/manager.hpp"
#include "overlay.hpp"
#include "draw.hpp"

static lg::log_domain log_display("display");
#define ERR_DP LOG_STREAM(err, log_display)
#define LOG_DP LOG_STREAM(info, log_display)
#define DBG_DP LOG_STREAM(debug, log_display)

static lg::log_domain log_engine("engine");
#define ERR_NG LOG_STREAM(err, log_engine)


game_display::game_display(game_board& board,
		std::weak_ptr<wb::manager> wb,
		reports& reports_object,
		const std::string& theme_id,
		const config& level)
	: display(&board, std::move(wb), reports_object, theme_id, level)
	, overlay_map_()
	, attack_indicator_src_()
	, attack_indicator_dst_()
	, route_()
	, displayedUnitHex_()
	, first_turn_(true)
	, in_game_(false)
	, chat_man_(new display_chat_manager(*this))
	, mode_(RUNNING)
	, needs_rebuild_(false)
{
}

game_display::~game_display()
{
	try {
		chat_man_->prune_chat_messages(true);
	} catch(...) {
		DBG_DP << "Caught exception in game_display destructor: " << utils::get_unknown_exception_type();
	}
}

void game_display::new_turn()
{
	if(!first_turn_) {
		const time_of_day& tod = resources::tod_manager->get_time_of_day();
		const time_of_day& old_tod = resources::tod_manager->get_previous_time_of_day();

		if(old_tod.image_mask != tod.image_mask) {
			fade_tod_mask(old_tod.image_mask, tod.image_mask);
		}
	}

	first_turn_ = false;

	update_tod();
}

void game_display::select_hex(map_location hex)
{
	if(hex.valid() && fogged(hex)) {
		return;
	}
	display::select_hex(hex);

	display_unit_hex(hex);
}

void game_display::highlight_hex(map_location hex)
{
	wb::future_map_if future(!synced_context::is_synced()); /**< Lasts for whole method. */

	const unit *u = context().get_visible_unit(hex, viewing_team(), !dont_show_all_);
	if (u) {
		displayedUnitHex_ = hex;
		invalidate_unit();
	} else {
		u = context().get_visible_unit(mouseoverHex_, viewing_team(), !dont_show_all_);
		if (u) {
			// mouse moved from unit hex to non-unit hex
			if (context().units().count(selectedHex_)) {
				displayedUnitHex_ = selectedHex_;
				invalidate_unit();
			}
		}
	}

	display::highlight_hex(hex);
	invalidate_game_status();
}


void game_display::display_unit_hex(map_location hex)
{
	if (!hex.valid())
		return;

	wb::future_map_if future(!synced_context::is_synced()); /**< Lasts for whole method. */

	const unit *u = context().get_visible_unit(hex, viewing_team(), !dont_show_all_);
	if (u) {
		displayedUnitHex_ = hex;
		invalidate_unit();
	}
}

void game_display::invalidate_unit_after_move(const map_location& src, const map_location& dst)
{
	if (src == displayedUnitHex_) {
		displayedUnitHex_ = dst;
		invalidate_unit();
	}
}

void game_display::scroll_to_leader(int side, SCROLL_TYPE scroll_type,bool force)
{
	unit_map::const_iterator leader = context().units().find_leader(side);

	if(leader.valid() && leader->is_visible_to_team(viewing_team(), false)) {
		scroll_to_tile(leader->get_location(), scroll_type, true, force);
	}
}

void game_display::update()
{
	display::update();

	if (std::shared_ptr<wb::manager> w = wb_.lock()) {
		w->pre_draw();
	}
	process_reachmap_changes();
	/**
	 * @todo FIXME: must modify changed, but best to do it at the
	 * floating_label level
	 */
	chat_man_->prune_chat_messages();
}


void game_display::render()
{
	display::render();

	if (std::shared_ptr<wb::manager> w = wb_.lock()) {
		w->post_draw();
	}
}

void game_display::draw_invalidated()
{
	display::draw_invalidated();
	if (fake_unit_man_->empty()) {
		return;
	}
	unit_drawer drawer = unit_drawer(*this);

	for(const unit* temp_unit : *fake_unit_man_) {
		const map_location& loc = temp_unit->get_location();
		if(utils::contains(invalidated_, loc) && unit_can_draw_here(loc, *temp_unit)) {
			drawer.redraw_unit(*temp_unit);
		}
	}
}

namespace
{
const image::locator mouseover_normal_top
	{"misc/hover-hex-top.png", "~RC(magenta>gold)"};
const image::locator mouseover_normal_bot
	{"misc/hover-hex-bottom.png", "~RC(magenta>gold)"};

const image::locator mouseover_enemy_top
	{"misc/hover-hex-enemy-top.png", "~RC(magenta>red)"};
const image::locator mouseover_enemy_bot
	{"misc/hover-hex-enemy-bottom.png", "~RC(magenta>red)"};

const image::locator mouseover_self_top
	{"misc/hover-hex-top.png", "~RC(magenta>green)"};
const image::locator mouseover_self_bot
	{"misc/hover-hex-bottom.png", "~RC(magenta>green)"};

const image::locator mouseover_ally_top
	{"misc/hover-hex-top.png", "~RC(magenta>lightblue)"};
const image::locator mouseover_ally_bot
	{"misc/hover-hex-bottom.png", "~RC(magenta>lightblue)"};

/**
 * Function to return 2 half-hex footsteps images for the given location.
 * Only loc is on the current route set by set_route.
 */
std::vector<texture> footsteps_images(const map_location& loc, const pathfind::marked_route& route, const display_context* dc)
{
	std::vector<texture> res;

	if (route.steps.size() < 2) {
		return res; // no real "route"
	}

	std::vector<map_location>::const_iterator i =
			 std::find(route.steps.begin(),route.steps.end(),loc);

	if( i == route.steps.end()) {
		return res; // not on the route
	}

	// Check which footsteps images of game_config we will use
	int move_cost = 1;
	const unit_map::const_iterator u = dc->units().find(route.steps.front());
	if(u != dc->units().end()) {
		move_cost = u->movement_cost(dc->map().get_terrain(loc));
	}
	int image_number = std::min<int>(move_cost, game_config::foot_speed_prefix.size());
	if (image_number < 1) {
		return res; // Invalid movement cost or no images
	}
	const std::string foot_speed_prefix = game_config::foot_speed_prefix[image_number-1];

	texture teleport;

	// We draw 2 half-hex (with possibly different directions),
	// but skip the first for the first step.
	const int first_half = (i == route.steps.begin()) ? 1 : 0;
	// and the second for the last step
	const int second_half = (i+1 == route.steps.end()) ? 0 : 1;

	for (int h = first_half; h <= second_half; ++h) {
		const std::string sense( h==0 ? "-in" : "-out" );

		if (!tiles_adjacent(*(i+(h-1)), *(i+h))) {
			std::string teleport_image =
			h==0 ? game_config::foot_teleport_enter : game_config::foot_teleport_exit;
			teleport = image::get_texture(teleport_image, image::HEXED);
			continue;
		}

		// In function of the half, use the incoming or outgoing direction
		map_location::direction dir = (i+(h-1))->get_relative_dir(*(i+h));

		std::string rotate;
		if (dir > map_location::direction::south_east) {
			// No image, take the opposite direction and do a 180 rotation
			dir = i->get_opposite_direction(dir);
			rotate = "~FL(horiz)~FL(vert)";
		}

		const std::string image = foot_speed_prefix
			+ sense + "-" + i->write_direction(dir)
			+ ".png" + rotate;

		res.push_back(image::get_texture(image, image::HEXED));
	}

	// we draw teleport image (if any) in last
	if (teleport != nullptr) res.push_back(teleport);

	return res;
}
} //anonymous namespace

void game_display::draw_hex(const map_location& loc)
{
	const bool on_map = context().map().on_board(loc);
	const bool is_shrouded = shrouded(loc);

	display::draw_hex(loc);

	if(cursor::get() == cursor::WAIT) {
		// Interaction is disabled, so we don't need anything else
		return;
	}

	if(on_map && loc == mouseoverHex_ && !map_screenshot_) {
		drawing_layer hex_top_layer = drawing_layer::mouseover_bottom;
		const unit* u = context().get_visible_unit(loc, viewing_team());
		if(u != nullptr) {
			hex_top_layer = drawing_layer::mouseover_top;
		}

		const image::locator* mo_top_path;
		const image::locator* mo_bot_path;

		if(u == nullptr) {
			mo_top_path = &mouseover_normal_top;
			mo_bot_path = &mouseover_normal_bot;
		} else if(viewing_team().is_enemy(u->side())) {
			mo_top_path = &mouseover_enemy_top;
			mo_bot_path = &mouseover_enemy_bot;
		} else if(viewing_team().side() == u->side()) {
			mo_top_path = &mouseover_self_top;
			mo_bot_path = &mouseover_self_bot;
		} else {
			mo_top_path = &mouseover_ally_top;
			mo_bot_path = &mouseover_ally_bot;
		}

		drawing_buffer_add(hex_top_layer, loc,
			[tex = image::get_texture(*mo_top_path, image::HEXED)](const rect& dest) { draw::blit(tex, dest); });

		drawing_buffer_add(drawing_layer::mouseover_bottom, loc,
			[tex = image::get_texture(*mo_bot_path, image::HEXED)](const rect& dest) { draw::blit(tex, dest); });
	}

	// Draw reach_map information.
	if(!is_shrouded && !reach_map_.empty() && reach_map_.find(loc) != reach_map_.end()) {
		// draw the reachmap tint below units and high terrain graphics
		std::string color = prefs::get().reach_map_color();
		std::string tint_opacity = std::to_string(prefs::get().reach_map_tint_opacity());

		drawing_buffer_add(drawing_layer::reachmap_highlight, loc, [tex = image::get_texture(game_config::reach_map_prefix + ".png~RC(magenta>"+color+")~O("+tint_opacity+"%)", image::HEXED)](const rect& dest) {
			draw::blit(tex, dest);
		});
		// We remove the reachmap border mask of the hovered hex to avoid weird interactions with other visual objects.
		if(loc != mouseoverHex_) {
			// draw the highlight borders on top of units and terrain
			drawing_buffer_add(drawing_layer::reachmap_border, loc, [images = get_reachmap_images(loc)](const rect& dest) {
				for(const texture& t : images) {
					draw::blit(t, dest);
				}
			});
		}
	}

	if(std::shared_ptr<wb::manager> w = wb_.lock()) {
		w->draw_hex(loc);

		if(!(w->is_active() && w->has_temp_move())) {
			std::vector<texture> footstepImages = footsteps_images(loc, route_, dc_);
			if(!footstepImages.empty()) {
				drawing_buffer_add(drawing_layer::footsteps, loc, [images = std::move(footstepImages)](const rect& dest) {
					for(const texture& t : images) {
						draw::blit(t, dest);
					}
				});
			}
		}
	}

	// Draw the attack direction indicator
	if(on_map && loc == attack_indicator_src_) {
		drawing_buffer_add(drawing_layer::attack_indicator, loc,
			[tex = image::get_texture("misc/attack-indicator-src-" + attack_indicator_direction() + ".png", image::HEXED)](const rect& dest)
		 	{ draw::blit(tex, dest); }
		);
	} else if(on_map && loc == attack_indicator_dst_) {
		drawing_buffer_add(drawing_layer::attack_indicator, loc,
			[tex = image::get_texture("misc/attack-indicator-dst-" + attack_indicator_direction() + ".png", image::HEXED)](const rect& dest)
			{ draw::blit(tex, dest); }
		);
	}

	// Linger overlay unconditionally otherwise it might give glitches
	// so it's drawn over the shroud and fog.
	if(mode_ != RUNNING) {
		static const image::locator linger(game_config::images::linger);
		drawing_buffer_add(drawing_layer::linger_overlay, loc,
			[tex = image::get_texture(linger, image::TOD_COLORED)](const rect& dest) { draw::blit(tex, dest); });
	}

	if(on_map && loc == selectedHex_ && !game_config::images::selected.empty()) {
		static const image::locator selected(game_config::images::selected);
		drawing_buffer_add(drawing_layer::selected_hex, loc,
			[tex = image::get_texture(selected, image::HEXED)](const rect& dest) { draw::blit(tex, dest); });
	}

	// Show def% and turn to reach info
	if(!is_shrouded && on_map) {
		draw_movement_info(loc);
	}
}

const time_of_day& game_display::get_time_of_day(const map_location& loc) const
{
	return resources::tod_manager->get_time_of_day(loc);
}

bool game_display::has_time_area() const
{
	return resources::tod_manager->has_time_area();
}

void game_display::layout()
{
	display::layout();

	// We need teams for the reports below
	if(context().teams().empty()) {
		return;
	}

	refresh_report("report_clock");
	refresh_report("report_battery");
	refresh_report("report_countdown");

	if (invalidateGameStatus_)
	{
		wb::future_map future; // start planned unit map scope

		// We display the unit the mouse is over if it is over a unit,
		// otherwise we display the unit that is selected.
		for (const std::string &name : reports_object_->report_list()) {
			refresh_report(name);
		}
		invalidateGameStatus_ = false;
	}
}


void game_display::set_game_mode(const game_mode mode)
{
	if(mode != mode_) {
		mode_ = mode;
		invalidate_all();
	}
}

void game_display::draw_movement_info(const map_location& loc)
{
	// Search if there is a mark here
	pathfind::marked_route::mark_map::iterator w = route_.marks.find(loc);

	std::shared_ptr<wb::manager> wb = wb_.lock();

	// Don't use empty route or the first step (the unit will be there)
	if(w != route_.marks.end()
				&& !route_.steps.empty() && route_.steps.front() != loc) {
		const unit_map::const_iterator un =
				(wb && wb->get_temp_move_unit().valid()) ?
						wb->get_temp_move_unit() : context().units().find(route_.steps.front());
		if(un != context().units().end()) {
			// Display the def% of this terrain
			int move_cost = un->movement_cost(context().map().get_terrain(loc));
			int def = (move_cost == movetype::UNREACHABLE ?
						0 : 100 - un->defense_modifier(context().map().get_terrain(loc)));
			std::stringstream def_text;
			def_text << def << "%";

			color_t color = game_config::red_to_green(def, false);

			// simple mark (no turn point) use smaller font
			int def_font = w->second.turns > 0 ? 18 : 16;
			draw_text_in_hex(loc, drawing_layer::move_info, def_text.str(), def_font, color);

			drawing_buffer_add(drawing_layer::move_info, loc,
				[inv = w->second.invisible, zoc = w->second.zoc, cap = w->second.capture](const rect& dest) {
					if(inv) {
						draw::blit(image::get_texture(image::locator{"misc/hidden.png"}, image::HEXED), dest);
					}

					if(zoc) {
						draw::blit(image::get_texture(image::locator{"misc/zoc.png"}, image::HEXED), dest);
					}

					if(cap) {
						draw::blit(image::get_texture(image::locator{"misc/capture.png"}, image::HEXED), dest);
					}
				});

			//we display turn info only if different from a simple last "1"
			if (w->second.turns > 1 || (w->second.turns == 1 && loc != route_.steps.back())) {
				std::stringstream turns_text;
				turns_text << w->second.turns;
				draw_text_in_hex(loc, drawing_layer::move_info, turns_text.str(), 17, font::NORMAL_COLOR, 0.5,0.8);
			}

			// The hex is full now, so skip the "show enemy moves"
			return;
		}
	}
	// When out-of-turn, it's still interesting to check out the terrain defs of the selected unit
	else if (selectedHex_.valid() && loc == mouseoverHex_)
	{
		const unit_map::const_iterator selectedUnit = resources::gameboard->find_visible_unit(selectedHex_,viewing_team());
		const unit_map::const_iterator mouseoveredUnit = resources::gameboard->find_visible_unit(mouseoverHex_,viewing_team());
		if(selectedUnit != context().units().end() && mouseoveredUnit == context().units().end()) {
			// Display the def% of this terrain
			int move_cost = selectedUnit->movement_cost(context().map().get_terrain(loc));
			int def = (move_cost == movetype::UNREACHABLE ?
						0 : 100 - selectedUnit->defense_modifier(context().map().get_terrain(loc)));
			std::stringstream def_text;
			def_text << def << "%";

			color_t color = game_config::red_to_green(def, false);

			// use small font
			int def_font = 16;
			draw_text_in_hex(loc, drawing_layer::move_info, def_text.str(), def_font, color);
		}
	}

	if (!reach_map_.empty()) {
		reach_map::iterator reach = reach_map_.find(loc);
		if (reach != reach_map_.end() && reach->second > 1) {
			const std::string num = std::to_string(reach->second);
			draw_text_in_hex(loc, drawing_layer::move_info, num, 16, font::YELLOW_COLOR);
		}
	}
}

void game_display::highlight_reach(const pathfind::paths &paths_list)
{
	unhighlight_reach();
	highlight_another_reach(paths_list);
}

void game_display::highlight_another_reach(const pathfind::paths &paths_list,
			const map_location& goal)
{
	// Fold endpoints of routes into reachability map.
	for (const pathfind::paths::step &dest : paths_list.destinations) {
		reach_map_[dest.curr]++;
	}
	reach_map_changed_ = true;

	if(goal != map_location::null_location() && paths_list.destinations.contains(goal)) {
		const auto& path_to_goal = paths_list.destinations.get_path(paths_list.destinations.find(goal));
		const map_location enemy_unit_location = path_to_goal[0];
		units_that_can_reach_goal_.insert(enemy_unit_location);
	}
}

bool game_display::unhighlight_reach()
{
	units_that_can_reach_goal_.clear();
	if(!reach_map_.empty()) {
		reach_map_.clear();
		reach_map_changed_ = true;
		return true;
	} else {
		return false;
	}
}

void game_display::invalidate_route()
{
	for(const map_location& step : route_.steps) {
		invalidate(step);
	}
}

void game_display::set_route(const pathfind::marked_route *route)
{
	invalidate_route();

	if(route != nullptr) {
		route_ = *route;
	} else {
		route_.steps.clear();
		route_.marks.clear();
	}

	invalidate_route();
}

void game_display::float_label(const map_location& loc, const std::string& text, const color_t& color)
{
	if(prefs::get().floating_labels() == false || fogged(loc)) {
		return;
	}

	rect loc_rect = get_location_rect(loc);

	using namespace std::chrono_literals;
	const auto lifetime = 1s / turbo_speed();

	// Base speed is 100 pixels per second, taken in milliseconds
	const double pixels_per_millisecond = 0.1 * turbo_speed() * get_zoom_factor();

	font::floating_label flabel(text);
	flabel.set_font_size(int(font::SIZE_FLOAT_LABEL * get_zoom_factor()));
	flabel.set_color(color);
	flabel.set_position(loc_rect.center().x, loc_rect.y); // middle of top edge
	flabel.set_move(0, -pixels_per_millisecond); // moving up
	flabel.set_lifetime(0ms, std::chrono::round<std::chrono::milliseconds>(lifetime));
	flabel.set_scroll_mode(font::ANCHOR_LABEL_MAP);

	font::add_floating_label(flabel);
}

void game_display::set_attack_indicator(const map_location& src, const map_location& dst)
{
	if (attack_indicator_src_ != src || attack_indicator_dst_ != dst) {
		invalidate(attack_indicator_src_);
		invalidate(attack_indicator_dst_);

		attack_indicator_src_ = src;
		attack_indicator_dst_ = dst;

		invalidate(attack_indicator_src_);
		invalidate(attack_indicator_dst_);
	}
}

void game_display::clear_attack_indicator()
{
	set_attack_indicator(map_location::null_location(), map_location::null_location());
}

void game_display::begin_game()
{
	in_game_ = true;
	create_buttons();
	invalidate_all();
}

void game_display::needs_rebuild(bool b) {
	if (b) {
		needs_rebuild_ = true;
	}
}

bool game_display::maybe_rebuild() {
	if (needs_rebuild_) {
		needs_rebuild_ = false;
		recalculate_minimap();
		invalidate_all();
		rebuild_all();
		return true;
	}
	return false;
}

display::overlay_map& game_display::get_overlays()
{
	return overlay_map_;
}

std::vector<texture> game_display::get_reachmap_images(const map_location& loc) const
{
	std::vector<std::string> names;
	const auto adjacent = get_adjacent_tiles(loc);

	enum visibility { REACH = 0, ENEMY = 1, CLEAR = 2 };
	std::array<visibility, 6> tiles;

	for(int i = 0; i < 6; ++i) {
		// look for units adjacent to loc
		std::string test_location = std::to_string(adjacent[i].x) + "," + std::to_string(adjacent[i].y);
		const unit *u = context().get_visible_unit(adjacent[i], viewing_team());
		if(reach_map_.find(adjacent[i]) != reach_map_.end()) {
			DBG_DP << test_location << " is REACHABLE";
			tiles[i] = REACH;
		}
		/**
		 * Make sure there is a unit selected or displayed when drawing the reachmap with enemy detection.
		 * Enemy detection needs to be "disabled" when the reach_map_team_index_ failsafes to viewing_team_index.
		 */
		else if(display::reach_map_team_index_ == display::viewing_team_index_) {
			DBG_DP << test_location << " is NOT REACHABLE";
			tiles[i] = CLEAR;
		}
		// Grab the reachmap-context team index updated in "display::process_reachmap_changes()" and test for adjacent enemy units
		else if(u != nullptr && resources::gameboard->get_team(display::reach_map_team_index_).is_enemy(u->side())) {
			DBG_DP << test_location << " has an ENEMY";
			tiles[i] = ENEMY;
		} else {
			DBG_DP << test_location << " is NOT REACHABLE";
			tiles[i] = CLEAR;
		}
	}

	// Find out if location is in the inner part of reachmap (surrounded by reach)
	int s;
	for(s = 0; s != 6; ++s) {
		if(tiles[s] != REACH) {
			break;
		}
	}

	if(s == 6) {
		// Completely surrounded by reach. This may have a special graphic.
		DBG_DP << "Tried completely surrounding";
		std::string name = game_config::reach_map_prefix + "-all.png";
		if(image::exists(name)) {
			names.push_back(std::move(name));
		}
	}

	// Find all the directions overlap occurs from
	for(int i = 0, cap1 = 0; cap1 != 6; ++cap1) {
		if(tiles[i] != REACH) {
			std::ostringstream stream;
			std::string suffix;
			std::string name;
			stream << game_config::reach_map_prefix;

			std::string color = prefs::get().reach_map_color();
			std::string enemy_color = prefs::get().reach_map_enemy_color();
			std::string border_opacity = std::to_string(prefs::get().reach_map_border_opacity());

			if(tiles[i] == ENEMY) {
				suffix = ".png~RC(magenta>"+enemy_color+")~O("+border_opacity+"%)";
			} else {
				suffix = ".png~RC(magenta>"+color+")~O("+border_opacity+"%)";
			}

			for(int cap2 = 0; tiles[i] != REACH && cap2 != 6; i = (i + 1) % 6, ++cap2) {
				stream << "-" << map_location::write_direction(map_location::direction{i});
				if(!image::exists(stream.str() + ".png")) {
					DBG_DP << "Image does not exist: " << stream.str() + ".png on " << loc;
					// If we don't have any surface at all,
					// then move onto the next overlapped area
					if(name.empty()) {
						i = (i + 1) % 6;
					}
					break;
				} else {
					name = stream.str();
				}
			}

			if(!name.empty()) {
				names.push_back(name + suffix);
			}
		} else {
			i = (i + 1) % 6;
		}
	}

	// now get the textures
	std::vector<texture> res;

	for(const std::string& name : names) {
		DBG_DP << "Pushing: " << name;
		if(texture tex = image::get_texture(name, image::HEXED)) {
			res.push_back(std::move(tex));
		}
	}

	return res;
}
