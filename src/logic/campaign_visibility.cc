/*
 * Copyright (C) 2007-2017 by the Widelands Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "logic/campaign_visibility.h"

#include <map>
#include <memory>

#include "base/log.h"
#include "graphic/graphic.h"
#include "io/filesystem/filesystem.h"
#include "logic/filesystem_constants.h"
#include "logic/map_objects/tribes/tribe_basic_info.h"
#include "profile/profile.h"
#include "scripting/lua_interface.h"

CampaignVisibility::CampaignVisibility() {
	g_fs->ensure_directory_exists(kSaveDir);

	// Get the current version of the campaign config
	LuaInterface lua;
	std::unique_ptr<LuaTable> table(lua.run_script("campaigns/campaigns.lua"));
	const int current_version = table->get_int("version");
	std::unique_ptr<Profile> campvis;

	// Create a new campvis file if there wasn't one
	if (!(g_fs->file_exists(kCampVisFile))) {
		// There is no campvis file - create one.
		campvis.reset(new Profile(kCampVisFile.c_str()));
		campvis->pull_section("global");
		campvis->get_safe_section("global").set_int("version", current_version - 1);
		campvis->pull_section("campaigns");
		campvis->pull_section("scenarios");
		campvis->write(kCampVisFile.c_str(), true);
	}

	// Handle legacy campvis file
	// TODO(GunChleoc): Remove after Build 21
	campvis.reset(new Profile(kCampVisFile.c_str()));
	if (campvis->get_section("campmaps") != nullptr) {
		update_legacy_campvis(current_version);
	}

	// Read difficulty images
	std::unique_ptr<LuaTable> difficulties_table(table->get_table("difficulties"));
	std::vector<std::pair<const std::string, const Image*>> difficulty_levels;
	for (const auto& difficulty_level_table : difficulties_table->array_entries<std::unique_ptr<LuaTable>>()) {
		difficulty_levels.push_back(std::make_pair(
												 _(difficulty_level_table->get_string("descname")),
												 g_gr->images().get(difficulty_level_table->get_string("image"))));
	}

	// Read solved scenarios
	campvis.reset(new Profile(kCampVisFile.c_str()));
	Section& campvis_scenarios = campvis->get_safe_section("scenarios");

	// Now read all the data from campaign config
	std::unique_ptr<LuaTable> campaigns_table(table->get_table("campaigns"));
	i18n::Textdomain td("maps");

	for (const auto& campaign_table : campaigns_table->array_entries<std::unique_ptr<LuaTable>>()) {
		CampaignData* campaign_data = new CampaignData();
		campaign_data->name = campaign_table->get_string("name");
		campaign_data->descname = _(campaign_table->get_string("descname"));
		campaign_data->tribename = Widelands::get_tribeinfo(campaign_table->get_string("tribe")).descname;
		campaign_data->description = _(campaign_table->get_string("description"));
		campaign_data->prerequisite = campaign_table->has_key("prerequisite") ? campaign_table->get_string("prerequisite") : "";
		campaign_data->visible = false;

		// Collect difficulty information
		std::unique_ptr<LuaTable> difficulty_table(campaign_table->get_table("difficulty"));
		campaign_data->difficulty_level = difficulty_table->get_int("level");
		campaign_data->difficulty_image = difficulty_levels.at(campaign_data->difficulty_level - 1).second;
		campaign_data->difficulty_description = difficulty_levels.at(campaign_data->difficulty_level - 1).first;
		const std::string difficulty_description = _(difficulty_table->get_string("description"));
		if (!difficulty_description.empty()) {
			campaign_data->difficulty_description =
					i18n::join_sentences(campaign_data->difficulty_description, difficulty_description);

		}

		// Scenarios
		std::unique_ptr<LuaTable> scenarios_table(campaign_table->get_table("scenarios"));
		for (const auto& scenario : scenarios_table->array_entries<std::unique_ptr<LuaTable>>()) {
			ScenarioData* scenario_data = new ScenarioData();
			// NOCOM scenario_data.index = counter + 1;
			scenario_data->path = scenario->get_string("path");
			if (campvis_scenarios.get_bool(scenario_data->path.c_str(), false)) {
				solved_scenarios_.insert(scenario_data->path);
			}

			scenario_data->descname = _(scenario->get_string("descname")); // NOCOM get this from the map later
			scenario_data->is_tutorial = false;
			scenario_data->playable = scenario_data->path != "dummy.wmf";
			scenario_data->visible = false; // NOCOM
			campaign_data->scenarios.push_back(std::unique_ptr<ScenarioData>(std::move(scenario_data)));
		}

		campaigns_.push_back(std::unique_ptr<CampaignData>(std::move(campaign_data)));
	}

	// Now calculate the visibility
	update_visibility_info();
}

void CampaignVisibility::update_visibility_info() {
	for (auto& campaign : campaigns_) {
		if (campaign->prerequisite.empty() || solved_scenarios_.count(campaign->prerequisite) == 1) {
			// A campaign is visible if its prerequisites have been fulfilled
			campaign->visible = true;
		} else {
			// A campaign is also visible if one of its scenarios has been solved
			for (size_t i = 0; i < campaign->scenarios.size(); ++i) {
				auto& scenario = campaign->scenarios.at(i);
				if (solved_scenarios_.count(scenario->path) == 1) {
					campaign->visible = true;
					break;
				}
			}
		}
		// Now set scenario visibility
		if (campaign->visible) {
			for (size_t i = 0; i < campaign->scenarios.size(); ++i) {
				auto& scenario = campaign->scenarios.at(i);
				if (i == 0) {
					// The first scenario in a visible campaign is always visible
					scenario->visible = true;
				} else {
					// A scenario is visible if its predecessor was solved
					scenario->visible = solved_scenarios_.count(campaign->scenarios.at(i-1)->path) == 1;
				}
				if (!scenario->visible) {
					// If a scenario is invisible, subsequent scenarios are also invisible
					break;
				}
			}
		}
	}
}

/**
 * Handle legacy campvis file
 */
// TODO(GunChleoc): Remove after Build 21
void CampaignVisibility::update_legacy_campvis(int version) {
	log("Converting legacy campvis\n");

	std::map<std::string, std::string> legacy_scenarios = {
		{"fri02.wmf", "frisians01"},
		{"fri01.wmf", "frisians00"},
		{"atl02.wmf", "atlanteans01"},
		{"atl01.wmf", "atlanteans00"},
		{"emp04.wmf", "empiretut03"},
		{"emp03.wmf", "empiretut02"},
		{"emp02.wmf", "empiretut01"},
		{"emp01.wmf", "empiretut00"},
		{"bar02.wmf", "barbariantut01"},
		{"bar01.wmf", "barbariantut00"},
	};

	Profile read_campvis(kCampVisFile.c_str());
	Section& campvis_scenarios = read_campvis.get_safe_section("campmaps");
	bool set_solved = false;
	std::set<std::string> solved_legacy_scenarios;
	for (const auto& legacy_scenario : legacy_scenarios) {
		if (set_solved) {
			solved_legacy_scenarios.insert(legacy_scenario.first);
		}
		set_solved = campvis_scenarios.get_bool(legacy_scenario.second.c_str(), false);
	}

	// Now write everything
	Profile write_campvis(kCampVisFile.c_str());
	write_campvis.pull_section("global").set_int("version", version);

	Section& write_scenarios = write_campvis.pull_section("scenarios");
	for (const auto& scenario : solved_legacy_scenarios) {
		write_scenarios.set_bool(scenario.c_str(), true);
	}

	write_campvis.write(kCampVisFile.c_str(), true);
}

/**
 * Marks the scenario with the given file 'path' as solved.
 */
void CampaignVisibility::mark_scenario_as_solved(const std::string& path) {
	assert(g_fs->file_exists(kCampVisFile));
	// NOCOM evaluate whether we need to update here
	solved_scenarios_.insert(path);
	update_visibility_info();

	// Write the campvis
	Profile campvis(kCampVisFile.c_str());
	campvis.pull_section("scenarios").set_bool(path.c_str(), true);
	campvis.write(kCampVisFile.c_str(), false);
}
