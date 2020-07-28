dirname = path.dirname(__file__)

tribes:new_productionsite_type {
   msgctxt = "empire_building",
   name = "empire_smelting_works",
   -- TRANSLATORS: This is a building name used in lists of buildings
   descname = pgettext("empire_building", "Smelting Works"),
   helptext_script = dirname .. "helptexts.lua",
   icon = dirname .. "menu.png",
   size = "medium",

   buildcost = {
      log = 1,
      granite = 4,
      marble = 2
   },
   return_on_dismantle = {
      granite = 3,
      marble = 1
   },

   animations = {
      idle = {
         pictures = path.list_files(dirname .. "idle_??.png"),
         hotspot = { 39, 53 },
      },
      build = {
         pictures = path.list_files(dirname .. "build_??.png"),
         hotspot = { 39, 53 },
      },
      working = {
         pictures = path.list_files(dirname .. "working_??.png"),
         hotspot = { 39, 53 },
         fps = 5
      },
   },

   aihints = {
      prohibited_till = 700,
      very_weak_ai_limit = 1,
      weak_ai_limit = 2
   },

   working_positions = {
      empire_smelter = 1
   },

   inputs = {
      { name = "coal", amount = 8 },
      { name = "iron_ore", amount = 8 },
      { name = "gold_ore", amount = 8 }
   },

   programs = {
      work = {
         -- TRANSLATORS: Completed/Skipped/Did not start working because ...
         descname = _"working",
         actions = {
            "call=smelt_iron",
            "call=smelt_gold",
            "call=smelt_iron_2",
         }
      },
      -- 2 identical programs for iron to prevent unnecessary skipping penalty
      smelt_iron = {
         -- TRANSLATORS: Completed/Skipped/Did not start smelting iron because ...
         descname = _"smelting iron",
         actions = {
            "return=skipped unless economy needs iron",
            "consume=iron_ore coal",
            "sleep=25000",
            "playsound=sound/metal/fizzle 150",
            "animate=working 35000",
            "playsound=sound/metal/ironping 80",
            "produce=iron"
         }
      },
      smelt_iron_2 = {
         -- TRANSLATORS: Completed/Skipped/Did not start smelting iron because ...
         descname = _"smelting iron",
         actions = {
            "return=skipped unless economy needs iron",
            "consume=iron_ore coal",
            "sleep=25000",
            "playsound=sound/metal/fizzle 150",
            "animate=working 35000",
            "playsound=sound/metal/ironping 80",
            "produce=iron"
         }
      },
      smelt_gold = {
         -- TRANSLATORS: Completed/Skipped/Did not start smelting gold because ...
         descname = _"smelting gold",
         actions = {
            "return=skipped unless economy needs gold",
            "consume=gold_ore coal",
            "sleep=25000",
            "playsound=sound/metal/fizzle 150",
            "animate=working 35000",
            "playsound=sound/metal/goldping 80",
            "produce=gold"
         }
      },
   },
}
