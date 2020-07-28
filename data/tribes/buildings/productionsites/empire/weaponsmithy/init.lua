dirname = path.dirname(__file__)

tribes:new_productionsite_type {
   msgctxt = "empire_building",
   name = "empire_weaponsmithy",
   -- TRANSLATORS: This is a building name used in lists of buildings
   descname = pgettext("empire_building", "Weapon Smithy"),
   helptext_script = dirname .. "helptexts.lua",
   icon = dirname .. "menu.png",
   size = "medium",

   buildcost = {
      log = 2,
      granite = 2,
      marble = 2,
      marble_column = 3
   },
   return_on_dismantle = {
      log = 1,
      granite = 1,
      marble = 1,
      marble_column = 2
   },

   animations = {
      idle = {
         pictures = path.list_files(dirname .. "idle_??.png"),
         hotspot = { 44, 61 },
      },
      build = {
         pictures = path.list_files(dirname .. "build_??.png"),
         hotspot = { 44, 61 },
      },
      working = {
         pictures = path.list_files(dirname .. "working_??.png"),
         hotspot = { 44, 61 },
         fps = 2
      },
   },

   aihints = {
      prohibited_till = 1300
   },

   working_positions = {
      empire_weaponsmith = 1
   },

   inputs = {
      { name = "planks", amount = 8 },
      { name = "coal", amount = 8 },
      { name = "iron", amount = 8 },
      { name = "gold", amount = 8 }
   },

   programs = {
      work = {
         -- TRANSLATORS: Completed/Skipped/Did not start working because ...
         descname = _"working",
         actions = {
            "call=produce_spear_wooden",
            "call=produce_spear",
            "call=produce_spear_advanced",
            "call=produce_spear_heavy",
            "call=produce_spear_war",
         }
      },
      produce_spear_wooden = {
         -- TRANSLATORS: Completed/Skipped/Did not start forging a wooden spear because ...
         descname = _"forging a wooden spear",
         actions = {
            -- time total: 50 + 3.6
            "return=skipped unless economy needs spear_wooden",
            "consume=planks",
            "sleep=20000",
            "playsound=sound/smiths/smith 192",
            "animate=working 21000",
            "playsound=sound/smiths/sharpening 120",
            "sleep=9000",
            "produce=spear_wooden"
         }
      },
      produce_spear = {
         -- TRANSLATORS: Completed/Skipped/Did not start forging a spear because ...
         descname = _"forging a spear",
         actions = {
            -- time total: 77 + 3.6
            "return=skipped unless economy needs spear",
            "consume=coal iron planks",
            "sleep=32000",
            "playsound=sound/smiths/smith 192",
            "animate=working 36000",
            "playsound=sound/smiths/sharpening 120",
            "sleep=9000",
            "produce=spear"
         }
      },
      produce_spear_advanced = {
         -- TRANSLATORS: Completed/Skipped/Did not start forging an advanced spear because ...
         descname = _"forging an advanced spear",
         actions = {
            -- time total: 77 + 3.6
            "return=skipped unless economy needs spear_advanced",
            "consume=coal iron:2 planks",
            "sleep=32000",
            "playsound=sound/smiths/smith 192",
            "animate=working 36000",
            "playsound=sound/smiths/sharpening 120",
            "sleep=9000",
            "produce=spear_advanced"
         }
      },
      produce_spear_heavy = {
         -- TRANSLATORS: Completed/Skipped/Did not start forging a heavy spear because ...
         descname = _"forging a heavy spear",
         actions = {
            -- time total: 77 + 3.6
            "return=skipped unless economy needs spear_heavy",
            "consume=coal:2 gold iron planks",
            "sleep=32000",
            "playsound=sound/smiths/smith 192",
            "animate=working 36000",
            "playsound=sound/smiths/sharpening 120",
            "sleep=9000",
            "produce=spear_heavy"
         }
      },
      produce_spear_war = {
         -- TRANSLATORS: Completed/Skipped/Did not start forging a war spear because ...
         descname = _"forging a war spear",
         actions = {
            -- time total: 77 + 3.6
            "return=skipped unless economy needs spear_war",
            "consume=coal:2 gold iron:2 planks",
            "sleep=32000",
            "playsound=sound/smiths/smith 192",
            "animate=working 36000",
            "playsound=sound/smiths/sharpening 120",
            "sleep=9000",
            "produce=spear_war"
         }
      },
   },
}
