dirname = path.dirname(__file__)

tribes:new_productionsite_type {
   msgctxt = "frisians_building",
   name = "frisians_goldmine_deep",
   -- TRANSLATORS: This is a building name used in lists of buildings
   descname = pgettext("frisians_building", "Deep Gold Mine"),
   helptext_script = dirname .. "helptexts.lua",
   icon = dirname .. "menu.png",
   size = "mine",

   enhancement_cost = {
      brick = 2,
      granite = 1,
      log = 1,
      thatch_reed = 2
   },
   return_on_dismantle_on_enhanced = {
      brick = 1,
      log = 1,
      thatch_reed = 1
   },

   animations = {
      idle = {
         pictures = path.list_files (dirname .. "idle_??.png"),
         hotspot = {27, 65},
         fps = 10,
      },
      working = {
         pictures = path.list_files (dirname .. "working_??.png"),
         hotspot = {27, 66},
         fps = 10,
      },
      empty = {
         pictures = path.list_files (dirname .. "empty_?.png"),
         hotspot = {27, 48},
      },
      unoccupied = {
         pictures = path.list_files (dirname .. "unoccupied_?.png"),
         hotspot = {27, 48},
      },
   },

   aihints = {
      mines = "gold",
   },

   working_positions = {
      frisians_miner = 1,
      frisians_miner_master = 1,
   },

   inputs = {
      { name = "meal", amount = 8 }
   },
   outputs = {
      "gold_ore"
   },

   programs = {
      work = {
         -- TRANSLATORS: Completed/Skipped/Did not start mining gold because ...
         descname = _"mining gold",
         actions = {
            "sleep=39800",
            "return=skipped unless economy needs coal",
            "consume=meal",
            "animate=working 19000",
            "animate=working 19000",
            "animate=working 19000",
            "mine=gold 3 100 50 5",
            "produce=gold",
            "sleep=2000",
            "mine=gold 3 100 1 5",
            "mine=gold 3 100 1 5",
            "produce=gold:2",
            "sleep=2000",
            "mine=gold 3 100 1 5",
            "mine=gold 3 100 1 5",
            "produce=gold:2"
         }
      },
   },
   out_of_resource_notification = {
      -- Translators: Short for "Out of ..." for a resource
      title = _"No Gold",
      heading = _"Main Gold Vein Exhausted",
      message =
         pgettext("frisians_building", "This gold mine’s main vein is exhausted. Expect strongly diminished returns on investment. This mine can’t be enhanced any further, so you should consider dismantling or destroying it."),
   },
}
