/*
 * Copyright (C) 2002-2004, 2006-2009 by the Widelands Development Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/**
 * @file
 * The entire transport subsystem comes into this file.
 *
 * What does _not_ belong in here: road renderer, client-side road building.
 *
 * What _does_ belong in here:
 * Flags, Roads, the logic behind ware pulls and pushes.
 *
 * \todo split this up into two files per class (.h and .cc)
*/

#include "transport.h"

#include "building.h"
#include "carrier.h"
#include "editor_game_base.h"
#include "game.h"
#include "instances.h"
#include "log.h"
#include "player.h"
#include "request.h"
#include "soldier.h"
#include "tribe.h"
#include <vector>
#include "warehouse.h"
#include "warehousesupply.h"
#include "wexception.h"
#include "widelands_fileread.h"
#include "widelands_filewrite.h"
#include "map_io/widelands_map_map_object_loader.h"
#include "map_io/widelands_map_map_object_saver.h"
#include "worker.h"

#include "upcast.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace Widelands {

Map_Object_Descr g_flag_descr("flag", "Flag");



/*
==============================================================================

Flag IMPLEMENTATION

==============================================================================
*/

/**
 * Create the flag. Initially, it doesn't have any attachments.
*/
Flag::Flag() :
PlayerImmovable(g_flag_descr),
m_anim(0),
m_building(0),
m_item_capacity(8),
m_item_filled(0),
m_items(new PendingItem[m_item_capacity]),
m_always_call_for_flag(0)
{
	for (uint32_t i = 0; i < 6; ++i) m_roads[i] = 0;
}

/**
 * Shouldn't be necessary to do anything, since die() always calls
 * cleanup() first.
*/
Flag::~Flag()
{
	if (m_item_filled)
		log("Flag: ouch! items left\n");
	delete[] m_items;

	if (m_building)
		log("Flag: ouch! building left\n");

	if (m_flag_jobs.size())
		log("Flag: ouch! flagjobs left\n");

	for (int32_t i = 0; i < 6; ++i)
		if (m_roads[i])
			log("Flag: ouch! road left\n");
}

/**
 * Create a flag at the given location
*/
Flag::Flag
	(Editor_Game_Base & egbase, Player & owning_player, Coords const coords)
	:
	PlayerImmovable       (g_flag_descr),
	m_anim                (0),
	m_building            (0),
	m_item_capacity       (8),
	m_item_filled         (0),
	m_items               (new PendingItem[m_item_capacity]),
	m_always_call_for_flag(0)
{
	for (uint32_t i = 0; i < 6; ++i) m_roads[i] = 0;

	set_owner(&owning_player);
	m_position = coords;

	upcast(Road, road, egbase.map().get_immovable(coords));
	//  we split a road, or a new, standalone flag is created
	(road ? road->get_economy() : new Economy(owning_player))->add_flag(*this);

	if (road)
		road->presplit(egbase, coords);
	init(egbase);
	if (road)
		road->postsplit(egbase, *this);
}

int32_t Flag::get_type() const throw ()
{
	return FLAG;
}

int32_t Flag::get_size() const throw ()
{
	return SMALL;
}

bool Flag::get_passable() const throw ()
{
	return true;
}


static std::string const flag_name = "flag";
std::string const & Flag::name() const throw () {return flag_name;}


Flag & Flag::base_flag()
{
	return *this;
}

/**
 * Call this only from Economy code!
*/
void Flag::set_economy(Economy *e)
{
	Economy *old = get_economy();

	if (old == e)
		return;

	PlayerImmovable::set_economy(e);

	for (int32_t i = 0; i < m_item_filled; ++i)
		m_items[i].item->set_economy(e);

	if (m_building)
		m_building->set_economy(e);

	container_iterate_const(FlagJobs, m_flag_jobs, i)
		i.current->request->set_economy(e);

	for (int8_t i = 0; i < 6; ++i) {
		if (m_roads[i])
			m_roads[i]->set_economy(e);
	}
}

/**
 * Call this only from the Building init!
*/
void Flag::attach_building(Editor_Game_Base & egbase, Building & building)
{
	assert(!m_building || m_building == &building);

	m_building = &building;

	Map const & map = egbase.map();
	egbase.set_road
		(map.get_fcoords(map.tl_n(m_position)), Road_SouthEast, Road_Normal);

	building.set_economy(get_economy());
}

/**
 * Call this only from the Building cleanup!
*/
void Flag::detach_building(Editor_Game_Base & egbase)
{
	assert(m_building);

	m_building->set_economy(0);

	Map const & map = egbase.map();
	egbase.set_road
		(map.get_fcoords(map.tl_n(m_position)), Road_SouthEast, Road_None);

	m_building = 0;
}

/**
 * Call this only from the Road init!
*/
void Flag::attach_road(int32_t const dir, Road * const road)
{
	assert(!m_roads[dir - 1] || m_roads[dir - 1] == road);

	m_roads[dir - 1] = road;
	m_roads[dir - 1]->set_economy(get_economy());
}

/**
 * Call this only from the Road init!
*/
void Flag::detach_road(int32_t const dir)
{
	assert(m_roads[dir - 1]);

	m_roads[dir - 1]->set_economy(0);
	m_roads[dir - 1] = 0;
}

/**
 * Return neighbouring flags.
*/
void Flag::get_neighbours(Neighbour_list *neighbours)
{
	for (int8_t i = 0; i < 6; ++i) {
		Road *road = m_roads[i];
		if (!road)
			continue;

		Neighbour n;
		n.road = road;
		n.flag = &road->get_flag(Road::FlagEnd);
		if (n.flag != this)
			n.cost = road->get_cost(Road::FlagStart);
		else {
			n.flag = &road->get_flag(Road::FlagStart);
			n.cost = road->get_cost(Road::FlagEnd);
		}

		assert(n.flag != this);
		neighbours->push_back(n);
	}

	// I guess this would be the place to add other ports if a port building
	// is attached to this flag
	// Or maybe other hosts of a carrier pigeon service, or a wormhole connection
	// point or whatever ;)
}

/**
 * Return the road that leads to the given flag.
*/
Road *Flag::get_road(Flag *flag)
{
	for (int8_t i = 0; i < 6; ++i) {
		Road *road = m_roads[i];
		if (!road)
			continue;

		if
			(&road->get_flag(Road::FlagStart) == flag ||
			 &road->get_flag(Road::FlagEnd)   == flag)
			return road;
	}

	return 0;
}


bool Flag::is_dead_end() const {
	if (get_building())
		return false;
	Flag const * first_other_flag = 0;
	for (uint8_t road_id = 6; road_id; --road_id)
		if (Road * const road = get_road(road_id)) {
			Flag & start = road->get_flag(Road::FlagStart);
			Flag & other = this == &start ? road->get_flag(Road::FlagEnd) : start;
			if (first_other_flag) {
				if (&other != first_other_flag)
					return false;
			} else
				first_other_flag = &other;
		}
	return true;
}


/**
 * Returns true if the flag can hold more items.
*/
bool Flag::has_capacity()
{
	return (m_item_filled < m_item_capacity);
}

/**
 * Signal the given bob by interrupting its task as soon as capacity becomes
 * free.
 *
 * The capacity queue is a simple FIFO queue.
 */
void Flag::wait_for_capacity(Game &, Worker & bob)
{
	m_capacity_wait.push_back(&bob);
}

/**
 * Remove the worker from the list of workers waiting for free capacity.
 */
void Flag::skip_wait_for_capacity(Game &, Worker & w)
{
	CapacityWaitQueue::iterator const it =
		std::find(m_capacity_wait.begin(), m_capacity_wait.end(), &w);
	if (it != m_capacity_wait.end())
		m_capacity_wait.erase(it);
}


void Flag::add_item(Game & game, WareInstance & item)
{

	assert(m_item_filled < m_item_capacity);

	PendingItem & pi = m_items[m_item_filled++];
	pi.item     = &item;
	pi.pending  = false;
	pi.nextstep = 0;

	item.set_location(game, this);
	item.update(game); //  will call call_carrier() if necessary
}

/**
 * \return true if an item is currently waiting for a carrier to the given Flag.
 *
 * \note Due to fetch_from_flag() semantics, this function makes no sense
 * for a  building destination.
*/
bool Flag::has_pending_item(Game &, Flag & dest) {
	int32_t i;

	for (i = 0; i < m_item_filled; ++i) {
		if (!m_items[i].pending)
			continue;

		if (m_items[i].nextstep != &dest)
			continue;

		return true;
	}

	return false;
}

/**
 * Called by carrier code to indicate that the carrier is moving to pick up an
 * item.
 * \return true if an item is actually waiting for the carrier.
*/
bool Flag::ack_pending_item(Game &, Flag & destflag) {
	int32_t i;

	for (i = 0; i < m_item_filled; ++i) {
		if (!m_items[i].pending)
			continue;

		if (m_items[i].nextstep != &destflag)
			continue;

		m_items[i].pending = false;
		return true;
	}

	return false;
}

/**
 * Wake one sleeper from the capacity queue.
*/
void Flag::wake_up_capacity_queue(Game & game)
{
	while (m_capacity_wait.size()) {
		Worker * const w = m_capacity_wait[0].get(game);
		m_capacity_wait.erase(m_capacity_wait.begin());
		if (w and w->wakeup_flag_capacity(game, *this))
			break;
	}
}

/**
 * Called by carrier code to retrieve one of the items on the flag that is meant
 * for that carrier.
 *
 * This function may return 0 even if \ref ack_pending_item() has already been
 * called successfully.
*/
WareInstance * Flag::fetch_pending_item(Game & game, PlayerImmovable & dest)
{
	int32_t best_index = -1;

	for (int32_t i = 0; i < m_item_filled; ++i) {
		if (m_items[i].nextstep != &dest)
			continue;

		// We prefer to retrieve items that have already been acked
		if (best_index < 0 || !m_items[i].pending)
			best_index = i;
	}

	if (best_index < 0)
		return 0;

	// move the other items up the list and return this one
	WareInstance * const item = m_items[best_index].item;
	--m_item_filled;
	memmove
		(&m_items[best_index], &m_items[best_index + 1],
		 sizeof(m_items[0]) * (m_item_filled - best_index));

	item->set_location(game, 0);

	// wake up capacity wait queue
	wake_up_capacity_queue(game);

	return item;
}

/**
 * Force a removal of the given item from this flag.
 * Called by \ref WareInstance::cleanup()
*/
void Flag::remove_item(Editor_Game_Base & egbase, WareInstance * const item)
{
	for (int32_t i = 0; i < m_item_filled; ++i) {
		if (m_items[i].item != item)
			continue;

		--m_item_filled;
		memmove
			(&m_items[i], &m_items[i + 1],
			 sizeof(m_items[0]) * (m_item_filled - i));

		if (upcast(Game, game, &egbase))
			wake_up_capacity_queue(*game);

		return;
	}

	throw wexception
		("MO(%u): Flag::remove_item: item %u not on flag",
		 serial(), item->serial());
}

/**
 * If nextstep is not null, a carrier will be called to move this item to
 * the given flag or building.
 *
 * If nextstep is null, the internal data will be reset to indicate that the
 * item isn't going anywhere right now.
 *
 * nextstep is compared with the cached data, and a new carrier is only called
 * if that data hasn't changed.
 *
 * This behaviour is overridden by m_always_call_for_step, which is set by
 * update_items() to ensure that new carriers are called when roads are
 * split, for example.
*/
void Flag::call_carrier
	(Game & game, WareInstance & item, PlayerImmovable * const nextstep)
{
	PendingItem * pi = 0;
	int32_t i = 0;

	// Find the PendingItem entry
	for (; i < m_item_filled; ++i) {
		if (m_items[i].item != &item)
			continue;

		pi = &m_items[i];
		break;
	}

	assert(pi);

	// Deal with the non-moving case quickly
	if (!nextstep) {
		pi->nextstep = 0;
		pi->pending = false;
		return;
	}

	// Find out whether we need to do anything
	if (pi->nextstep == nextstep && pi->nextstep != m_always_call_for_flag)
		return; // no update needed

	pi->nextstep = nextstep;
	pi->pending = false;

	// Deal with the building case
	if (nextstep == get_building()) {
		molog
			("Flag::call_carrier(%u): Tell building to fetch this item\n",
			 item.serial());

		if (!get_building()->fetch_from_flag(game)) {
			pi->item->cancel_moving();
			pi->item->update(game);
		}

		return;
	}

	// Deal with the normal (flag) case
	dynamic_cast<Flag const &>(*nextstep);

	for (int32_t dir = 1; dir <= 6; ++dir) {
		Road * const road = get_road(dir);
		Flag *       other;
		Road::FlagId flagid;

		if (!road)
			continue;

		if (&road->get_flag(Road::FlagStart) == this) {
			flagid = Road::FlagStart;
			other = &road->get_flag(Road::FlagEnd);
		} else {
			flagid = Road::FlagEnd;
			other = &road->get_flag(Road::FlagStart);
		}

		if (other != nextstep)
			continue;

		// Yes, this is the road we want; inform it
		if (road->notify_ware(game, flagid))
			return;

		// If the road doesn't react to the ware immediately, we try other roads:
		// They might lead to the same flag!
	}

	// Nothing found, just let it be picked up by somebody
	pi->pending = true;
	return;
}

/**
 * Called whenever a road gets broken or split.
 * Make sure all items on this flag are rerouted if necessary.
 *
 * \note When two roads connect the same two flags, and one of these roads
 * is removed, this might cause the carrier(s) on the other road to
 * move unnecessarily. Fixing this could potentially be very expensive and
 * fragile.
 * A similar thing can happen when a road is split.
*/
void Flag::update_items(Game & game, Flag * const other)
{
	m_always_call_for_flag = other;

	for (int32_t i = 0; i < m_item_filled; ++i)
		m_items[i].item->update(game);

	m_always_call_for_flag = 0;
}

void Flag::init(Editor_Game_Base & egbase)
{
	PlayerImmovable::init(egbase);

	set_position(egbase, m_position);

	m_anim = owner().tribe().get_flag_anim();
	m_animstart = egbase.get_gametime();
}

/**
 * Detach building and free roads.
*/
void Flag::cleanup(Editor_Game_Base & egbase)
{
	//molog("Flag::cleanup\n");

	while (m_flag_jobs.size()) {
		delete m_flag_jobs.begin()->request;
		m_flag_jobs.erase(m_flag_jobs.begin());
	}

	while (m_item_filled) {
		WareInstance & item = *m_items[--m_item_filled].item;

		item.set_location(dynamic_cast<Game &>(egbase), 0);
		item.destroy     (dynamic_cast<Game &>(egbase));
	}

	//molog("  items destroyed\n");

	if (m_building) {
		m_building->remove(egbase); //  immediate death
		assert(!m_building);
	}

	for (int8_t i = 0; i < 6; ++i) {
		if (m_roads[i]) {
			m_roads[i]->remove(egbase); //  immediate death
			assert(!m_roads[i]);
		}
	}

	get_economy()->remove_flag(*this);

	unset_position(egbase, m_position);

	//molog("  done\n");

	PlayerImmovable::cleanup(egbase);
}

/**
 * Destroy the building as well.
 *
 * \note This is needed in addition to the call to m_building->remove() in
 * \ref Flag::cleanup(). This function is needed to ensure a fire is created
 * when a player removes a flag.
*/
void Flag::destroy(Editor_Game_Base & egbase)
{
	if (m_building) {
		m_building->destroy(egbase);
		assert(!m_building);
	}

	PlayerImmovable::destroy(egbase);
}

/**
 * Add a new flag job to request the worker with the given ID, and to execute
 * the given program once it's completed.
*/
void Flag::add_flag_job
	(Game &, Ware_Index const workerware, std::string const & programname)
{
	FlagJob j;

	j.request =
		new Request
			(*this, workerware, Flag::flag_job_request_callback, Request::WORKER);
	j.program = programname;

	m_flag_jobs.push_back(j);
}

/**
 * This function is called when one of the flag job workers arrives on
 * the flag. Give him his job.
*/
void Flag::flag_job_request_callback
	(Game            &       game,
	 Request         &       rq,
	 Ware_Index,
	 Worker          * const w,
	 PlayerImmovable &       target)
{
	Flag & flag = dynamic_cast<Flag &>(target);

	assert(w);

	container_iterate(FlagJobs, flag.m_flag_jobs, i)
		if (i.current->request == &rq) {
			delete &rq;

			w->start_task_program(game, i.current->program);

			flag.m_flag_jobs.erase(i.current);
			return;
		}

	flag.molog("BUG: flag_job_request_callback: worker not found in list\n");
}

/*
==============================================================================

Road IMPLEMENTATION

==============================================================================
*/

// dummy instance because Map_Object needs a description
Map_Object_Descr g_road_descr("road", "Road");

/**
 * Most of the actual work is done in init.
*/
Road::Road() :
PlayerImmovable  (g_road_descr),
m_type           (0),
m_desire_carriers(0),
m_carrier_request(0)
{
	m_flags[0] = m_flags[1] = 0;
	m_flagidx[0] = m_flagidx[1] = -1;
}

/**
 * Most of the actual work is done in cleanup.
 */
Road::~Road()
{
	if (m_carrier_request) {
		log("Road::~Road: carrier request left\n");
		delete m_carrier_request;
	}
}

/**
 * Create a road between the given flags, using the given path.
*/
void Road::create
	(Editor_Game_Base & egbase,
	 Flag & start, Flag & end, Path const & path,
	 bool    const create_carrier,
	 int32_t const type)
{
	assert(start.get_position() == path.get_start());
	assert(end  .get_position() == path.get_end  ());
	assert(start.get_owner   () == end .get_owner());

	Player & owner          = start.owner();
	Road & road             = *new Road();
	road.set_owner(&owner);
	road.m_type             = type;
	road.m_flags[FlagStart] = &start;
	road.m_flags[FlagEnd]   = &end;
	// m_flagidx is set when attach_road() is called, i.e. in init()
	road.set_path(egbase, path);
	if (create_carrier) {
		Coords idle_position = start.get_position();
		{
			Map const & map = egbase.map();
			Path::Step_Vector::size_type idle_index = road.get_idle_index();
			for (Path::Step_Vector::size_type i = 0; i < idle_index; ++i)
				map.get_neighbour(idle_position, path[i], &idle_position);
		}
		Tribe_Descr const & tribe = owner.tribe();
		Carrier & carrier =
			dynamic_cast<Carrier &>
				(tribe.get_worker_descr(tribe.worker_index("carrier"))->create
				 	(egbase, owner, &start, idle_position));
		carrier.start_task_road(dynamic_cast<Game &>(egbase));
		road.m_carrier = &carrier;
	}
	log("Road::create: &road = %p\n", &road);
	road.init(egbase);
}

int32_t Road::get_type() const throw ()
{
	return ROAD;
}

int32_t Road::get_size() const throw ()
{
	return SMALL;
}

bool Road::get_passable() const throw ()
{
	return true;
}


static std::string const road_name = "road";
std::string const & Road::name() const throw () {return road_name;}


Flag & Road::base_flag()
{
	return *m_flags[FlagStart];
}

/**
 * Return the cost of getting from fromflag to the other flag.
*/
int32_t Road::get_cost(FlagId fromflag)
{
	return m_cost[fromflag];
}

/**
 * Set the new path, calculate costs.
 * You have to set start and end flags before calling this function.
*/
void Road::set_path(Editor_Game_Base & egbase, Path const & path)
{
	assert(path.get_nsteps() >= 2);
	assert(path.get_start() == m_flags[FlagStart]->get_position());
	assert(path.get_end() == m_flags[FlagEnd]->get_position());

	m_path = path;
	egbase.map().calc_cost(path, &m_cost[FlagStart], &m_cost[FlagEnd]);

	// Figure out where carriers should idle
	m_idle_index = path.get_nsteps() / 2;
}

/**
 * Add road markings to the map
*/
void Road::mark_map(Editor_Game_Base & egbase)
{
	Map & map = egbase.map();
	FCoords curf = map.get_fcoords(m_path.get_start());

	const Path::Step_Vector::size_type nr_steps = m_path.get_nsteps();
	for (Path::Step_Vector::size_type steps = 0; steps <= nr_steps; ++steps) {
		if (steps > 0 && steps < m_path.get_nsteps())
			set_position(egbase, curf);

		// mark the road that leads up to this field
		if (steps > 0) {
			const Direction dir  = get_reverse_dir(m_path[steps - 1]);
			const Direction rdir = 2 * (dir - Map_Object::WALK_E);

			if (rdir <= 4)
				egbase.set_road(curf, rdir, m_type);
		}

		// mark the road that leads away from this field
		if (steps < m_path.get_nsteps()) {
			const Direction dir  = m_path[steps];
			const Direction rdir = 2 * (dir - Map_Object::WALK_E);

			if (rdir <= 4)
				egbase.set_road(curf, rdir, m_type);

			map.get_neighbour(curf, dir, &curf);
		}
	}
}

/**
 * Remove road markings from the map
*/
void Road::unmark_map(Editor_Game_Base & egbase) {
	Map & map = egbase.map();
	FCoords curf(m_path.get_start(), &map[m_path.get_start()]);

	const Path::Step_Vector::size_type nr_steps = m_path.get_nsteps();
	for (Path::Step_Vector::size_type steps = 0; steps <= nr_steps; ++steps) {
		if (steps > 0 && steps < m_path.get_nsteps())
			unset_position(egbase, curf);

		// mark the road that leads up to this field
		if (steps > 0) {
			const Direction dir  = get_reverse_dir(m_path[steps - 1]);
			const Direction rdir = 2 * (dir - Map_Object::WALK_E);

			if (rdir <= 4)
				egbase.set_road(curf, rdir, Road_None);
		}

		// mark the road that leads away from this field
		if (steps < m_path.get_nsteps()) {
			const Direction  dir = m_path[steps];
			const Direction rdir = 2 * (dir - Map_Object::WALK_E);

			if (rdir <= 4)
				egbase.set_road(curf, rdir, Road_None);

			map.get_neighbour(curf, dir, &curf);
		}
	}
}

/**
 * Initialize the road.
*/
void Road::init(Editor_Game_Base & egbase)
{
	PlayerImmovable::init(egbase);

	if (2 <= m_path.get_nsteps())
		link_into_flags(egbase);
}

/**
 * This links into the flags, calls a carrier
 * and so on. This was formerly done in init (and
 * still is for normal games). But for save game loading
 * we needed to have this road already registered
 * as Map Object, thats why this is moved
 */
void Road::link_into_flags(Editor_Game_Base & egbase) {
	assert(m_path.get_nsteps() >= 2);

	// Link into the flags (this will also set our economy)

	{
		const Direction dir = m_path[0];
		m_flags[FlagStart]->attach_road(dir, this);
		m_flagidx[FlagStart] = dir;
	}


	const Direction dir =
		get_reverse_dir(m_path[m_path.get_nsteps() - 1]);
	m_flags[FlagEnd]->attach_road(dir, this);
	m_flagidx[FlagEnd] = dir;

	Economy::check_merge(*m_flags[FlagStart], *m_flags[FlagEnd]);

	// Mark Fields
	mark_map(egbase);

	if (upcast(Game, game, &egbase)) {
		Carrier * const carrier =
			static_cast<Carrier *>(m_carrier.get(*game));
		m_desire_carriers = 1;
		if (carrier) {
			//  This happens after a road split. Tell the carrier what's going on.
			carrier->set_location    (this);
			carrier->update_task_road(*game);
		} else if (not m_carrier_request)
			request_carrier(*game);
	}
}

/**
 * Cleanup the road
*/
void Road::cleanup(Editor_Game_Base & egbase)
{
	Game & game = dynamic_cast<Game &>(egbase);

	// Release carrier
	m_desire_carriers = 0;

	delete m_carrier_request;
	m_carrier_request = 0;

	m_carrier = 0; // carrier will be released via PlayerImmovable::cleanup

	// Unmark Fields
	unmark_map(game);

	// Unlink from flags (also clears the economy)
	m_flags[FlagStart]->detach_road(m_flagidx[FlagStart]);
	m_flags[FlagEnd]->detach_road(m_flagidx[FlagEnd]);

	Economy::check_split(*m_flags[FlagStart], *m_flags[FlagEnd]);

	m_flags[FlagStart]->update_items(game, m_flags[FlagEnd]);
	m_flags[FlagEnd]->update_items(game, m_flags[FlagStart]);

	PlayerImmovable::cleanup(game);
}

/**
 * Workers' economies are fixed by PlayerImmovable, but we need to handle
 * any requests ourselves.
*/
void Road::set_economy(Economy * const e)
{
	PlayerImmovable::set_economy(e);
	if (m_carrier_request)
		m_carrier_request->set_economy(e);
}

/**
 * Request a new carrier.
 *
 * Only call this if the road can handle a new carrier, and if no request has
 * been issued.
*/
void Road::request_carrier
#ifndef NDEBUG
	(Game & game)
#else
	(Game &)
#endif
{
	assert(!m_carrier.get(game) && !m_carrier_request);

	m_carrier_request =
		new Request
			(*this,
			 owner().tribe().safe_worker_index("carrier"),
			 Road::request_carrier_callback,
			 Request::WORKER);
}

/**
 * The carrier has arrived successfully.
*/
void Road::request_carrier_callback
	(Game            &       game,
	 Request         &       rq,
	 Ware_Index,
	 Worker          * const w,
	 PlayerImmovable &       target)
{
	assert(w);

	Road    & road    = dynamic_cast<Road    &>(target);
	Carrier & carrier = dynamic_cast<Carrier &>(*w);

	delete &rq;
	road.m_carrier_request = 0;

	road.m_carrier = &carrier;
	carrier.start_task_road(game);
}

/**
 * If we lost our carrier, re-request it.
*/
void Road::remove_worker(Worker & w)
{
	Editor_Game_Base & egbase = owner().egbase();
	Carrier * carrier = dynamic_cast<Carrier *>(m_carrier.get(egbase));

	if (carrier == &w)
		m_carrier = carrier = 0;

	if (not carrier and not m_carrier_request and m_desire_carriers)
		if (upcast(Game, game, &egbase))
			request_carrier(*game);

	PlayerImmovable::remove_worker(w);
}

/**
 * A flag has been placed that splits this road. This function is called before
 * the new flag initializes. We remove markings to avoid interference with the
 * flag.
*/
void Road::presplit(Editor_Game_Base & egbase, Coords) {unmark_map(egbase);}

/**
 * The flag that splits this road has been initialized. Perform the actual
 * splitting.
 *
 * After the split, this road will span [start...new flag]. A new road will
 * be created to span [new flag...end]
*/
void Road::postsplit(Editor_Game_Base & egbase, Flag & flag)
{
	Flag & oldend = *m_flags[FlagEnd];

	// detach from end
	oldend.detach_road(m_flagidx[FlagEnd]);

	// build our new path and the new road's path
	Map & map = egbase.map();
	CoordPath path(map, m_path);
	CoordPath secondpath(path);
	int32_t const index = path.get_index(flag.get_position());

	assert(index > 0);
	assert(static_cast<uint32_t>(index) < path.get_nsteps() - 1);

	path.truncate(index);
	secondpath.starttrim(index);

	// change road size and reattach
	m_flags[FlagEnd] = &flag;
	set_path(egbase, path);

	const Direction dir = get_reverse_dir(m_path[m_path.get_nsteps() - 1]);
	m_flags[FlagEnd]->attach_road(dir, this);
	m_flagidx[FlagEnd] = dir;

	// recreate road markings
	mark_map(egbase);

	// create the new road
	Road & newroad = *new Road();
	newroad.set_owner(get_owner());
	newroad.m_type = m_type;
	newroad.m_flags[FlagStart] = &flag; //  flagidx will be set on init()
	newroad.m_flags[FlagEnd] = &oldend;
	newroad.set_path(egbase, secondpath);

	// Find workers on this road that need to be reassigned
	// The algorithm is pretty simplistic, and has a bias towards keeping
	// the worker around; there's obviously nothing wrong with that.
	upcast(Carrier, carrier, m_carrier.get(egbase));
	std::vector<Worker *> const workers = get_workers();
	std::vector<Worker *> reassigned_workers;

	container_iterate_const(std::vector<Worker *>, workers, i) {
		Worker & w = **i.current;
		int32_t idx = path.get_index(w.get_position());

		// Careful! If the worker is currently inside the building at our
		// starting flag, we *must not* reassign him.
		// If he is in the building at our end flag or at the other road's
		// end flag, he can be reassigned to the other road.
		if (idx < 0) {
			if
				(dynamic_cast<Building const *>
				 	(map.get_immovable(w.get_position())))
			{
				Coords pos;
				map.get_brn(w.get_position(), &pos);
				if (pos == path.get_start())
					idx = 0;
			}
		}

		molog("Split: check %u -> idx %i\n", w.serial(), idx);

		if (idx < 0) {
			reassigned_workers.push_back(&w);

			if (carrier == &w) {
				// Reassign the carrier. Note that the final steps of reassigning
				// are done in newroad->init()
				m_carrier = 0;
				newroad.m_carrier = carrier;
			}
		}

		// Cause a worker update in any case
		if (upcast(Game, game, &egbase))
			w.send_signal(*game, "road");
	}

	// Initialize the new road
	newroad.init(egbase);

	// Actually reassign workers after the new road has initialized,
	// so that the reassignment is safe
	container_iterate_const(std::vector<Worker *>, reassigned_workers, i)
		(*i.current)->set_location(&newroad);

	// Do the following only if in game
	if (upcast(Game, game, &egbase)) {

		// Request a new carrier for this road if necessary
		// This must be done _after_ the new road initializes, otherwise request
		// routing might not work correctly
		if (!m_carrier.get(egbase) && !m_carrier_request)
			request_carrier(*game);

		// Make sure items waiting on the original endpoint flags are dealt with
		m_flags[FlagStart]->update_items(*game, &oldend);
		oldend.update_items(*game, m_flags[FlagStart]);
	}
}

/**
 * Called by Flag code: an item should be picked up from the given flag.
 * \return true if a carrier has been sent on its way, false otherwise.
 */
bool Road::notify_ware(Game & game, FlagId const flagid)
{
	if (upcast(Carrier, carrier, m_carrier.get(game)))
		return carrier->notify_ware(game, flagid);
	return false;
}



/*
==============================================================================

SupplyList IMPLEMENTATION

==============================================================================
*/

/**
 * Add a supply to the list.
*/
void SupplyList::add_supply(Supply & supp)
{
	m_supplies.push_back(&supp);
}

/**
 * Remove a supply from the list.
*/
void SupplyList::remove_supply(Supply & supp)
{
	container_iterate(Supplies, m_supplies, i)
		if (*i.current == &supp) {
			*i.current = *(i.end - 1);
			return m_supplies.pop_back();
		}

	throw wexception("SupplyList::remove: not in list");
}


/*
==============================================================================

WaresQueue IMPLEMENTATION

==============================================================================
*/

/**
 * Pre-initialize a WaresQueue
*/
WaresQueue::WaresQueue
	(PlayerImmovable &       _owner,
	 Ware_Index        const _ware,
	 uint8_t           const _size,
	 uint8_t           const _filled)
	:
	m_owner           (_owner),
	m_ware            (_ware),
	m_size            (_size),
	m_filled          (_filled),
	m_consume_interval(0),
	m_request         (0),
	m_callback_fn     (0),
	m_callback_data   (0)
{}


/**
 * Clear the queue appropriately.
*/
void WaresQueue::cleanup() {
	assert(m_ware);

	if (uint8_t const count = m_filled)
		m_owner.get_economy()->remove_wares(m_ware, count);

	m_filled = 0;
	m_size = 0;

	update();

	m_ware = Ware_Index::Null();
}

/**
 * Fix filled <= size and requests.
 * You must call this after every call to set_*()
*/
void WaresQueue::update() {
	assert(m_ware);

	if (m_filled > m_size) {
		m_owner.get_economy()->remove_wares(m_ware, m_filled - m_size);
		m_filled = m_size;
	}

	if (m_filled < m_size)
	{
		if (!m_request)
			m_request =
				new Request
					(m_owner,
					 m_ware,
					 WaresQueue::request_callback,
					 Request::WARE);

		m_request->set_count(m_size - m_filled);
		m_request->set_required_interval(m_consume_interval);
	}
	else
	{
		delete m_request;
		m_request = 0;
	}
}

/**
 * Set the callback function that is called when an item has arrived.
*/
void WaresQueue::set_callback(callback_t * const fn, void * const data)
{
	m_callback_fn = fn;
	m_callback_data = data;
}

/**
 * Called when an item arrives at the owning building.
*/
void WaresQueue::request_callback
	(Game            &       game,
	 Request         &,
	 Ware_Index        const ware,
#ifndef NDEBUG
	 Worker          * const w,
#else
	 Worker          *,
#endif
	 PlayerImmovable & target)
{
	WaresQueue & wq = dynamic_cast<Building &>(target).waresqueue(ware);

	assert(!w); // WaresQueue can't hold workers
	assert(wq.m_filled < wq.m_size);
	assert(wq.m_ware == ware);

	// Update
	wq.set_filled(wq.m_filled + 1);
	wq.update();

	if (wq.m_callback_fn)
		(*wq.m_callback_fn)(game, &wq, ware, wq.m_callback_data);
}

/**
 * Remove the wares in this queue from the given economy (used in accounting).
*/
void WaresQueue::remove_from_economy(Economy & e)
{
	if (m_ware) {
		e.remove_wares(m_ware, m_filled);
		if (m_request)
			m_request->set_economy(0);
	}
}

/**
 * Add the wares in this queue to the given economy (used in accounting)
*/
void WaresQueue::add_to_economy(Economy & e)
{
	if (m_ware) {
		e.add_wares(m_ware, m_filled);
		if (m_request)
			m_request->set_economy(&e);
	}
}

/**
 * Change size of the queue.
 *
 * \warning You must call \ref update() after this!
 * \todo Why not call update from here?
*/
void WaresQueue::set_size(const uint32_t size) throw ()
{
	m_size = size;
}

/**
 * Change fill status of the queue.
 *
 * \warning You must call \ref update() after this!
 * \todo Why not call update from here?
 */
void WaresQueue::set_filled(const uint32_t filled) throw () {
	if (filled > m_filled)
		m_owner.get_economy()->add_wares(m_ware, filled - m_filled);
	else if (filled < m_filled)
		m_owner.get_economy()->remove_wares(m_ware, m_filled - filled);

	m_filled = filled;
}

/**
 * Set the time between consumption of items when the owning building
 * is consuming at full speed.
 *
 * This interval is merely a hint for the Supply/Request balancing code.
*/
void WaresQueue::set_consume_interval(const uint32_t time) throw ()
{m_consume_interval = time;}

/**
 * Read and write
 */
#define WARES_QUEUE_DATA_PACKET_VERSION 1
void WaresQueue::Write
	(FileWrite & fw, Editor_Game_Base & egbase, Map_Map_Object_Saver * os)
{

	fw.Unsigned16(WARES_QUEUE_DATA_PACKET_VERSION);

	//  Owner and callback is not saved, but this should be obvious on load.
	fw.CString
		(owner().tribe().get_ware_descr(m_ware)->name().c_str());
	fw.Signed32(m_size);
	fw.Signed32(m_filled);
	fw.Signed32(m_consume_interval);
	if (m_request) {
		fw.Unsigned8(1);
		m_request->Write(fw, egbase, os);
	} else
		fw.Unsigned8(0);
}


void WaresQueue::Read
	(FileRead & fr, Editor_Game_Base & egbase, Map_Map_Object_Loader * ol)
{
	uint16_t const packet_version = fr.Unsigned16();
	if (packet_version == WARES_QUEUE_DATA_PACKET_VERSION) {
		delete m_request;
		m_ware             = owner().tribe().ware_index(fr.CString  ());
		m_size             =                            fr.Signed32 ();
		m_filled           =                            fr.Signed32 ();
		m_consume_interval =                            fr.Signed32 ();
		if                                             (fr.Unsigned8()) {
			m_request =
				new Request
					(m_owner,
					 Ware_Index::First(),
					 WaresQueue::request_callback,
					 Request::WORKER);
			m_request->Read(fr, egbase, ol);
		} else
			m_request = 0;

		//  Now Economy stuff. We have to add our filled items to the economy.
		add_to_economy(*m_owner.get_economy());
	} else
		throw wexception
			("WaresQueue::Read: Unknown WaresQueueVersion %u!", packet_version);
}

/*
==============================================================================

Economy IMPLEMENTATION

==============================================================================
*/

Economy::Economy(Player & player) :
	m_owner(player),
m_rebuilding(false),
m_request_timerid(0),
mpf_cycle(0)
{
	Tribe_Descr const & tribe = player.tribe();
	Ware_Index const nr_wares = tribe.get_nrwares();
	m_workers.set_nrwares(tribe.get_nrworkers());
	m_wares.set_nrwares(nr_wares);

	player.add_economy(*this);

	m_target_quantities = new Target_Quantity[nr_wares.value()];
	for (Ware_Index i = Ware_Index::First(); i < nr_wares; ++i) {
		Target_Quantity tq;
		tq.temporary = tq.permanent =
			tribe.get_ware_descr(i)->default_target_quantity();
		tq.last_modified = 0;
		m_target_quantities[i.value()] = tq;
	}

}

Economy::~Economy()
{
	assert(!m_rebuilding);

	m_owner.remove_economy(*this);

	if (m_requests.size())
		log("Warning: Economy still has requests left on destruction\n");
	if (m_flags.size())
		log("Warning: Economy still has flags left on destruction\n");
	if (m_warehouses.size())
		log("Warning: Economy still has warehouses left on destruction\n");

	delete[] m_target_quantities;
}


/**
 * \return an arbitrary flag in this economy, or 0 if no flag exists
 */
Flag & Economy::get_arbitrary_flag()
{
	assert(m_flags.size());
	return *m_flags[0];
}

/**
 * Two flags have been connected; check whether their economies should be
 * merged.
 * Since we could merge into both directions, we preserve the economy that is
 * currently bigger (should be more efficient).
*/
void Economy::check_merge(Flag & f1, Flag & f2)
{
	Economy * e1 = f1.get_economy();
	Economy * e2 = f2.get_economy();
	if (e1 != e2) {
		if (e1->get_nrflags() < e2->get_nrflags())
			std::swap(e1, e2);
		e1->do_merge(*e2);
	}
}

/// If the two flags can no longer reach each other (pathfinding!), the economy
/// gets split.
///
/// Should we create the new economy starting at f1 or f2? Ideally, we'd split
/// off in a way that the new economy will be relatively small.
///
/// Unfortunately, there's no easy way to tell in advance which of the two
/// resulting economies will be smaller (the problem is even NP-complete), so
/// we use a heuristic.
/// NOTE There is a way; parallel counting. If for example one has size 100 and
/// NOTE the other has size 1, we start counting (to 1) in the first. Then we
/// NOTE switch to the second and count (to 1) there. Then we switch to the
/// NOTE first and count (to 2) there. Then we switch to the second and have
/// NOTE nothing more to count. We are done and know that the second is not
/// NOTE larger than the first.
/// NOTE
/// NOTE We have not done more than n * (s + 1) counting operations, where n is
/// NOTE the number of parallel entities (2 in this example) and s is the size
/// NOTE of the smallest entity (1 in this example). So instead of risking to
/// NOTE make a bad guess and change 100 entities, we count 4 and change 1.
/// NOTE                                                                --sigra
///
/// Using f2 is just a guess, but if anything f2 is probably best: it will be
/// the end point of a road. Since roads are typically built from the center of
/// a country outwards, and since splits are more likely to happen outwards,
/// the economy at the end point is probably smaller in average. It's all just
/// guesswork though ;)
/// NOTE Many roads are built when a new building has just been placed. For
/// NOTE those cases, the guess is bad because the user typically builds from
/// NOTE the new building's flag to some existing flag (at the headquarter or
/// NOTE somewhere in his larger road network). This is also what the user
/// NOTE interface makes the player do when it enters roadbuilding mode after
/// NOTE placing a flag that is not connected with roads.               --sigra
void Economy::check_split(Flag & f1, Flag & f2)
{
	assert(&f1 != &f2);
	assert(f1.get_economy() == f2.get_economy());

	Economy & e = *f1.get_economy();

	if (not e.find_route(f1, f2, 0, false))
		e.do_split(f2);
}

/**
 * Provides the flexible priority queue to maintain the open list.
 *
 * This is more flexible than a standard priority_queue (fast boost() to
 * adjust cost)
*/
struct FlagQueue {
	FlagQueue() {}
	~FlagQueue() {}

	void flush() {m_data.clear();}

	// Return the best node and readjust the tree
	// Basic idea behind the algorithm:
	//  1. the top slot of the tree is empty
	//  2. if this slot has both children:
	//       fill this slot with one of its children or with slot[_size], whichever
	//       is best;
	//       if we filled with slot[_size], stop
	//       otherwise, repeat the algorithm with the child slot
	//     if it doesn't have any children (l >= _size)
	//       put slot[_size] in its place and stop
	//     if only the left child is there
	//       arrange left child and slot[_size] correctly and stop
	Flag * pop()
	{
		if (m_data.empty())
			return 0;

		Flag * head = m_data[0];

		uint32_t const nsize = m_data.size() - 1;
		uint32_t fix = 0;
		while (fix < nsize) {
			uint32_t l = fix * 2 + 1;
			uint32_t r = fix * 2 + 2;
			if (l >= nsize) {
				m_data[fix] = m_data[nsize];
				m_data[fix]->mpf_heapindex = fix;
				break;
			}
			if (r >= nsize) {
				if (m_data[nsize]->cost() <= m_data[l]->cost()) {
					m_data[fix] = m_data[nsize];
					m_data[fix]->mpf_heapindex = fix;
				} else {
					m_data[fix] = m_data[l];
					m_data[fix]->mpf_heapindex = fix;
					m_data[l] = m_data[nsize];
					m_data[l]->mpf_heapindex = l;
				}
				break;
			}

			if (m_data[nsize]->cost() <= m_data[l]->cost() && m_data[nsize]->cost() <= m_data[r]->cost()) {
				m_data[fix] = m_data[nsize];
				m_data[fix]->mpf_heapindex = fix;
				break;
			}
			if (m_data[l]->cost() <= m_data[r]->cost()) {
				m_data[fix] = m_data[l];
				m_data[fix]->mpf_heapindex = fix;
				fix = l;
			} else {
				m_data[fix] = m_data[r];
				m_data[fix]->mpf_heapindex = fix;
				fix = r;
			}
		}

		m_data.pop_back();

		debug(0, "pop");

		head->mpf_heapindex = -1;

		return head;
	}

	// Add a new node and readjust the tree
	// Basic idea:
	//  1. Put the new node in the last slot
	//  2. If parent slot is worse than self, exchange places and recurse
	// Note that I rearranged this a bit so swap isn't necessary
	void push(Flag *t)
	{
		uint32_t slot = m_data.size();
		m_data.push_back(static_cast<Flag *>(0));

		while (slot > 0) {
			uint32_t parent = (slot - 1) / 2;

			if (m_data[parent]->cost() < t->cost())
				break;

			m_data[slot] = m_data[parent];
			m_data[slot]->mpf_heapindex = slot;
			slot = parent;
		}
		m_data[slot] = t;
		t->mpf_heapindex = slot;

		debug(0, "push");
	}

	// Rearrange the tree after a node has become better, i.e. move the
	// node up
	// Pushing algorithm is basically the same as in push()
	void boost(Flag *t)
	{
		uint32_t slot = t->mpf_heapindex;

		assert(slot < m_data.size());
		assert(m_data[slot] == t);

		while (slot > 0) {
			uint32_t parent = (slot - 1) / 2;

			if (m_data[parent]->cost() <= t->cost())
				break;

			m_data[slot] = m_data[parent];
			m_data[slot]->mpf_heapindex = slot;
			slot = parent;
		}
		m_data[slot] = t;
		t->mpf_heapindex = slot;

		debug(0, "boost");
	}

	// Recursively check integrity
	void debug(uint32_t node, char const * const str)
	{
		uint32_t l = node * 2 + 1;
		uint32_t r = node * 2 + 2;
		if (m_data[node]->mpf_heapindex != static_cast<int32_t>(node)) {
			fprintf(stderr, "%s: mpf_heapindex integrity!\n", str);
			abort();
		}
		if (l < m_data.size()) {
			if (m_data[node]->cost() > m_data[l]->cost()) {
				fprintf(stderr, "%s: Integrity failure\n", str);
				abort();
			}
			debug(l, str);
		}
		if (r < m_data.size()) {
			if (m_data[node]->cost() > m_data[r]->cost()) {
				fprintf(stderr, "%s: Integrity failure\n", str);
				abort();
			}
			debug(r, str);
		}
	}

private:
	std::vector<Flag *> m_data;
};

/**
 * Calcaluate a route between two flags.
 *
 * The calculated route is stored in route if it exists.
 *
 * For two flags from the same economy, this function should always be
 * successful, except when it's called from check_split()
 *
 * \note route will be cleared before storing the result.
 *
 * \param start, end start and endpoint of the route
 * \param route the calculated route
 * \param wait UNDOCUMENTED
 * \param cost_cutoff maximum cost for desirable routes. If no route cheaper
 * than this can be found, return false
 *
 * \return true if a route has been found, false otherwise
 *
 * \todo Document parameter wait
*/
bool Economy::find_route
	(Flag & start, Flag & end,
	 Route * const route,
	 bool    const wait,
	 int32_t const cost_cutoff)
{
	assert(start.get_economy() == this);
	assert(end  .get_economy() == this);

	Map & map = owner().egbase().map();

	// advance the path-finding cycle
	++mpf_cycle;
	if (!mpf_cycle) { // reset all cycle fields
		for (uint32_t i = 0; i < m_flags.size(); ++i)
			m_flags[i]->mpf_cycle = 0;
		++mpf_cycle;
	}

	// Add the starting flag into the open list
	FlagQueue Open;
	Flag *current;

	start.mpf_cycle    = mpf_cycle;
	start.mpf_backlink = 0;
	start.mpf_realcost = 0;
	start.mpf_estimate =
		map.calc_cost_estimate(start.get_position(), end.get_position());

	Open.push(&start);

	while ((current = Open.pop())) {
		if (current == &end)
			break; // found our goal

		if (cost_cutoff >= 0 && current->mpf_realcost > cost_cutoff)
			return false;

		// Loop through all neighbouring flags
		Neighbour_list neighbours;

		current->get_neighbours(&neighbours);

		for (uint32_t i = 0; i < neighbours.size(); ++i) {
			Flag * const neighbour = neighbours[i].flag;
			int32_t cost;
			int32_t wait_cost = 0;

			//  No need to find the optimal path when only checking connectivity.
			if (neighbour == &end && !route)
				return true;

			if (wait)
				wait_cost =
					(current->m_item_filled + neighbour->m_item_filled)
					*
					neighbours[i].cost
					/
					2;
			cost = current->mpf_realcost + neighbours[i].cost + wait_cost;

			if (neighbour->mpf_cycle != mpf_cycle) {
				// add to open list
				neighbour->mpf_cycle = mpf_cycle;
				neighbour->mpf_realcost = cost;
				neighbour->mpf_estimate = map.calc_cost_estimate
					(neighbour->get_position(), end.get_position());
				neighbour->mpf_backlink = current;
				Open.push(neighbour);
			} else if (cost + neighbour->mpf_estimate < neighbour->cost()) {
				// found a better path to a field that's already Open
				neighbour->mpf_realcost = cost;
				neighbour->mpf_backlink = current;
				if (neighbour->mpf_heapindex != -1) // This neighbour is already 'popped', skip it
					Open.boost(neighbour);
			}
		}
	}

	if (!current) // path not found
		return false;

	// Unwind the path to form the route
	if (route) {
		route->clear();
		route->m_totalcost = end.mpf_realcost;

		for (Flag * flag = &end;; flag = flag->mpf_backlink) {
			route->m_route.insert(route->m_route.begin(), flag);
			if (flag == &start)
				break;
		}
	}

	return true;
}


/**
 * Add a flag to the flag array.
 * Only call from Flag init and split/merger code!
*/
void Economy::add_flag(Flag & flag)
{
	assert(flag.get_economy() == 0);

	m_flags.push_back(&flag);
	flag.set_economy(this);
	flag.mpf_cycle = 0;
}

/**
 * Remove a flag from the flag array.
 * Only call from Flag cleanup and split/merger code!
*/
void Economy::remove_flag(Flag & flag)
{
	assert(flag.get_economy() == this);

	do_remove_flag(flag);

	// automatically delete the economy when it becomes empty.
	if (m_flags.empty())
		delete this;
}

/**
 * Remove the flag, but don't delete the economy automatically.
 * This is called from the merge code.
*/
void Economy::do_remove_flag(Flag & flag)
{
	flag.set_economy(0);

	// fast remove
	container_iterate(Flags, m_flags, i)
		if (*i.current == &flag) {
			*i.current = *(i.end - 1);
			return m_flags.pop_back();
		}
	throw wexception("trying to remove nonexistent flag");
}

/**
 * Call this whenever some entity created a ware, e.g. when a lumberjack
 * has felled a tree.
 * This is also called when a ware is added to the economy through trade or
 * a merger.
*/
void Economy::add_wares(Ware_Index const id, uint32_t const count)
{
	//log("%p: add(%i, %i)\n", this, id, count);

	m_wares.add(id, count);

	// TODO: add to global player inventory?
}
void Economy::add_workers(Ware_Index const id, uint32_t const count)
{
	//log("%p: add(%i, %i)\n", this, id, count);

	m_workers.add(id, count);

	// TODO: add to global player inventory?
}

/**
 * Call this whenever a ware is destroyed or consumed, e.g. food has been
 * eaten or a warehouse has been destroyed.
 * This is also called when a ware is removed from the economy through trade or
 * a split of the Economy.
*/
void Economy::remove_wares(Ware_Index const id, uint32_t const count)
{
	assert(id < m_owner.tribe().get_nrwares());
	//log("%p: remove(%i, %i) from %i\n", this, id, count, m_wares.stock(id));

	m_wares.remove(id, count);

	Target_Quantity & tq = m_target_quantities[id.value()];
	tq.temporary =
		tq.temporary <= tq.permanent + count ?
		tq.permanent : tq.temporary - count;

	// TODO: remove from global player inventory?
}

/**
 * Call this whenever a worker is destroyed.
 * This is also called when a worker is removed from the economy through
 * a split of the Economy.
 */
void Economy::remove_workers(Ware_Index const id, uint32_t const count)
{
	//log("%p: remove(%i, %i) from %i\n", this, id, count, m_workers.stock(id));

	m_workers.remove(id, count);

	// TODO: remove from global player inventory?
}

/**
 * Add the warehouse to our list of warehouses.
 * This also adds the wares in the warehouse to the economy. However, if wares
 * are added to the warehouse in the future, add_wares() must be called.
*/
void Economy::add_warehouse(Warehouse *wh)
{
	m_warehouses.push_back(wh);
}

/**
 * Remove the warehouse and its wares from the economy.
*/
void Economy::remove_warehouse(Warehouse *wh)
{
	uint32_t i;
	for (i = 0; i < m_warehouses.size(); ++i)
		if (m_warehouses[i] == wh) {
			if (i < m_warehouses.size() - 1)
				m_warehouses[i] = m_warehouses[m_warehouses.size() - 1];
			break;
		}


	//  This assert was modified, since on loading, warehouses might try to
	//  remove themselves from their own economy, though they weren't added
	//  (since they weren't initialized)
	assert(i != m_warehouses.size() || m_warehouses.empty());

	if (m_warehouses.size())
		m_warehouses.pop_back();
}

/**
 * Consider the request, try to fulfill it immediately or queue it for later.
 * Important: This must only be called by the \ref Request class.
*/
void Economy::add_request(Request & req)
{
	assert(req.is_open());
	assert(!have_request(req));

	assert(&owner());

	m_requests.push_back(&req);

	// Try to fulfill the request
	start_request_timer();
}

/**
 * \return true if the given Request is registered with the \ref Economy, false
 * otherwise
*/
bool Economy::have_request(Request & req)
{
	return
		std::find(m_requests.begin(), m_requests.end(), &req)
		!=
		m_requests.end();
}

/**
 * Remove the request from this economy.
 * Important: This must only be called by the \ref Request class.
*/
void Economy::remove_request(Request & req)
{
	RequestList::iterator const it =
		std::find(m_requests.begin(), m_requests.end(), &req);

	if (it == m_requests.end()) {
		log("WARNING: remove_request(%p) not in list\n", &req);
		return;
	}

	*it = *m_requests.rbegin();

	m_requests.pop_back();
}

/**
 * Add a supply to our list of supplies.
*/
void Economy::add_supply(Supply & supply)
{
	m_supplies.add_supply(supply);
	start_request_timer();
}


/**
 * Remove a supply from our list of supplies.
*/
void Economy::remove_supply(Supply & supply)
{
	m_supplies.remove_supply(supply);
}


bool Economy::needs_ware(Ware_Index const ware_type) const {
	size_t const nr_supplies = m_supplies.get_nrsupplies();
	uint32_t const t = target_quantity(ware_type).temporary;
	uint32_t quantity = 0;
	for (size_t i = 0; i < nr_supplies; ++i)
		if (upcast(WarehouseSupply const, warehouse_supply, &m_supplies[i])) {
			quantity += warehouse_supply->stock_wares(ware_type);
			if (t <= quantity)
				return false;
		}
	return true;
}


/**
 * Add e's flags to this economy.
 *
 * Also transfer all wares and wares request. Try to resolve the new ware
 * requests if possible.
*/
void Economy::do_merge(Economy & e)
{
	for (Ware_Index::value_t i = m_owner.tribe().get_nrwares().value(); i;) {
		--i;
		Target_Quantity other_tq = e.m_target_quantities[i];
		Target_Quantity & this_tq = m_target_quantities[i];
		if (this_tq.last_modified < other_tq.last_modified)
			this_tq = other_tq;
	}

	//  If the options window for e is open, but not the one for *this, the user
	//  should still have an options window after the merge. Create an options
	//  window for *this where the options window for e is, to give the user
	//  some continuity.
	if
		(e.m_optionswindow_registry.window and
		 not m_optionswindow_registry.window)
	{
		m_optionswindow_registry.x = e.m_optionswindow_registry.x;
		m_optionswindow_registry.x = e.m_optionswindow_registry.x;
		show_options_window();
	}


	m_rebuilding = true;

	// Be careful around here. The last e->remove_flag() will cause the other
	// economy to delete itself.
	for (std::vector<Flag *>::size_type i = e.get_nrflags() + 1; --i;) {
		assert(i == e.get_nrflags());

		Flag & flag = *e.m_flags[0];

		e.do_remove_flag(flag);
		add_flag(flag);
	}

	// Fix up Supply/Request after rebuilding
	m_rebuilding = false;

	// implicitly delete the economy
	delete &e;
}

/// Flag initial_flag and all its direct and indirect neighbours are put into a
/// new economy.
void Economy::do_split(Flag & initial_flag)
{
	Economy & e = *new Economy(m_owner);

	for (Ware_Index::value_t i = m_owner.tribe().get_nrwares().value(); i;) {
		--i;
		e.m_target_quantities[i] = m_target_quantities[i];
	}

	m_rebuilding = true;
	e.m_rebuilding = true;

	// Use a vector instead of a set to ensure parallel simulation
	std::vector<Flag *> open;

	open.push_back(&initial_flag);
	while (open.size()) {
		Flag & flag = **open.rbegin();
		open.pop_back();

		if (flag.get_economy() != this)
			continue;

		// move this flag to the new economy
		remove_flag(flag);
		e.add_flag(flag);

		//  check all neighbours; if they aren't in the new economy yet, add them
		// to the list (note: roads and buildings are reassigned via Flag::set_economy)
		Neighbour_list neighbours;
		flag.get_neighbours(&neighbours);

		for (uint32_t i = 0; i < neighbours.size(); ++i) {
			Flag & n = *neighbours[i].flag;

			if (n.get_economy() == this)
				open.push_back(&n);
		}
	}

	// Fix Supply/Request after rebuilding
	m_rebuilding = false;
	e.m_rebuilding = false;

	// As long as rebalance commands are tied to specific flags, we
	// need this, because the flag that rebalance commands for us were
	// tied to might have been moved into the other economy
	start_request_timer();
}

/**
 * Make sure the request timer is running.
 */
void Economy::start_request_timer(int32_t const delta)
{
	if (upcast(Game, game, &m_owner.egbase()))
		game->cmdqueue().enqueue
			(new Cmd_Call_Economy_Balance
			 	(game->get_gametime() + delta, this, m_request_timerid));
}


/**
 * Find the supply that is best suited to fulfill the given request.
 * \return 0 if no supply is found, the best supply otherwise
*/
Supply * Economy::find_best_supply
	(Game & game, Request const & req, int32_t & cost)
{
	assert(req.is_open());

	Route buf_route0, buf_route1;
	Supply *best_supply = 0;
	Route *best_route = 0;
	int32_t best_cost = -1;
	Flag & target_flag = req.target_flag();

	for (size_t i = 0; i < m_supplies.get_nrsupplies(); ++i) {
		Supply & supp = m_supplies[i];

		// idle requests only get active supplies
		if (req.is_idle() and not supp.is_active()) {
			/* unless the warehouse REALLY needs the supply */
			if (req.get_priority(0) > 100) { //  100 is the 'real idle' priority
				//check if the supply is at current target
				if (&target_flag == &supp.get_position(game)->base_flag()) {
					//assert(false);
					continue;
				}
			} else if (not supp.is_active()) {
				continue;
			}
		}

		// Check requirements
		if (!supp.nr_supplies(game, req))
			continue;

		Route * const route =
			best_route != &buf_route0 ? &buf_route0 : &buf_route1;
		// will be cleared by find_route()

		if
			(!
			 find_route
			 	(supp.get_position(game)->base_flag(),
			 	 target_flag,
			 	 route,
			 	 false,
			 	 best_cost))
		{
			if (!best_route)
				throw wexception
					("Economy::find_best_supply: COULD NOT FIND A ROUTE!");
			continue;
		}

		best_supply = &supp;
		best_route = route;
		best_cost = route->get_totalcost();
	}

	if (!best_route)
		return 0;

	cost = best_cost;
	return best_supply;
}

struct RequestSupplyPair {
	bool is_item;
	bool is_worker;
	Ware_Index ware;
	TrackPtr<Request> request;
	TrackPtr<Supply>  supply;
	int32_t priority;

	/**
	 * pairid is an explicit tie-breaker for comparison.
	 *
	 * Without it, the pair priority queue would use an implicit, system dependent
	 * tie breaker, which in turn causes desyncs.
	 */
	uint32_t pairid;

	struct Compare {
		bool operator()
			(RequestSupplyPair const & p1, RequestSupplyPair const & p2)
		{
			return
				p1.priority == p2.priority ? p1.pairid < p2.pairid :
				p1.priority <  p2.priority;
		}
	};
};

typedef
std::priority_queue
<RequestSupplyPair, std::vector<RequestSupplyPair>, RequestSupplyPair::Compare>
RSPairQueue;

struct RSPairStruct {
	RSPairQueue queue;
	uint32_t pairid;
	int32_t nexttimer;

	RSPairStruct() : pairid(0) {}
};

/**
 * Walk all Requests and find potential transfer candidates.
*/
void Economy::process_requests(Game & game, RSPairStruct & s)
{
	container_iterate_const(RequestList, m_requests, i) {
		Request & req = **i.current;
		int32_t cost; // estimated time in milliseconds to fulfill Request

		// We somehow get desynced request lists that don't trigger desync
		// alerts, so add info to the sync stream here.
		{
			::StreamWrite & ss = game.syncstream();
			ss.Unsigned8 (req.get_type  ());
			ss.Unsigned8 (req.get_index ().value());
			ss.Unsigned32(req.target    ().serial());
		}

		Ware_Index const ware_index = req.get_index();
		Supply * const supp = find_best_supply(game, req, cost);

		if (!supp)
			continue;

		if (!req.is_idle() and not supp->is_active()) {
			// Calculate the time the building will be forced to idle waiting
			// for the request
			int32_t const idletime =
				game.get_gametime() + 15000 + 2 * cost - req.get_required_time();
			// If the building wouldn't have to idle, we wait with the request
			if (idletime < -200) {
				if (s.nexttimer < 0 || s.nexttimer > -idletime)
					s.nexttimer = -idletime;

				continue;
			}
		}

		int32_t const priority = req.get_priority (cost);
		if (priority < 0)
			continue;

		// Otherwise, consider this request/supply pair for queueing
		RequestSupplyPair rsp;

		rsp.is_item = false;
		rsp.is_worker = false;

		switch (req.get_type()) {
		case Request::WARE:    rsp.is_item    = true; break;
		case Request::WORKER:  rsp.is_worker  = true; break;
		default:
			assert(false);
		}

		rsp.ware = ware_index;
		rsp.request  = &req;
		rsp.supply = supp;
		rsp.priority = priority;
		rsp.pairid = ++s.pairid;

		s.queue.push(rsp);
	}

	// TODO: This function should be called from time to time
	create_requested_workers (game);
}


/**
 * Walk all Requests and find requests of workers than aren't supplied. Then
 * try to create the worker at warehouses.
*/
void Economy::create_requested_workers(Game & game)
{
	/*
		Find the request of workers that can not be supplied
	*/
	if (get_nr_warehouses() > 0) {
		Tribe_Descr const & tribe = owner().tribe();
		container_iterate_const(RequestList, m_requests, j) {
			Request const & req = **j.current;

			if (!req.is_idle() && req.get_type() == Request::WORKER) {
				Ware_Index const index = req.get_index();
				int32_t num_wares = 0;
				Worker_Descr * const w_desc = tribe.get_worker_descr(index);

				// Ignore it if is a worker that cann't be buildable
				if (!w_desc->buildable())
					continue;

				for (size_t i = 0; i < m_supplies.get_nrsupplies(); ++i)
					num_wares += m_supplies[i].nr_supplies(game, req);

				// If there aren't enough supplies...
				if (num_wares == 0) {
					bool created_worker = false;
					uint32_t n_wh = 0;
					while (n_wh < get_nr_warehouses()) {
						if (m_warehouses[n_wh]->can_create_worker(game, index)) {
							m_warehouses[n_wh]->create_worker(game, index);
							created_worker = true;
							//break;
						} // if (m_warehouses[n_wh]
						++n_wh;
					} // while (n_wh < get_nr_warehouses())
					if (! created_worker) { //  fix to nearest warehouse
						Warehouse & nearest = *m_warehouses[0];
						Worker_Descr::Buildcost const & cost = w_desc->buildcost();
						container_iterate_const(Worker_Descr::Buildcost, cost, bc_it)
							if
								(Ware_Index const w_id =
								 	tribe.ware_index(bc_it.current->first.c_str()))
								nearest.set_needed(w_id, bc_it.current->second);
					}
				} // if (num_wares == 0)
			} // if (req->is_open())
		} // for (RequestList::iterator
	} // if (get_nr_warehouses())
}

/**
 * Balance Requests and Supplies by collecting and weighing pairs, and
 * starting transfers for them.
*/
void Economy::balance_requestsupply(uint32_t const timerid)
{
	if (m_request_timerid != timerid)
		return;
	++m_request_timerid;

	RSPairStruct rsps;
	rsps.nexttimer = -1;

	if (upcast(Game, game, &m_owner.egbase())) {

		//  Try to fulfill non-idle Requests.
		process_requests(*game, rsps);

		//  Now execute request/supply pairs.
		while (rsps.queue.size()) {
			RequestSupplyPair rsp = rsps.queue.top();

			rsps.queue.pop();

			if
				(!rsp.request               ||
				 !rsp.supply                ||
				 !have_request(*rsp.request) ||
				 !rsp.supply->nr_supplies(*game, *rsp.request))
			{
				rsps.nexttimer = 200;
				continue;
			}

			rsp.request->start_transfer(*game, *rsp.supply);
			rsp.request->set_last_request_time(owner().egbase().get_gametime());

			//  for multiple wares
			if (rsp.request && have_request(*rsp.request))
				rsps.nexttimer = 200;
		}

		if (rsps.nexttimer > 0) //  restart the timer, if necessary
			start_request_timer(rsps.nexttimer);
	}
}

#define CURRENT_ECONOMY_VERSION 2

void Economy::Read(FileRead & fr, Game &, Map_Map_Object_Loader *)
{
	uint16_t const version = fr.Unsigned16();

	try {
		if (1 <= version and version <= CURRENT_ECONOMY_VERSION) {
			if (2 <= version)
				try {
					Tribe_Descr const & tribe = owner().tribe();
					while (Time const last_modified = fr.Unsigned32()) {
						char const * const ware_type_name = fr.CString();
						uint32_t const permanent = fr.Unsigned32();
						uint32_t const temporary = fr.Unsigned32();
						Ware_Index const ware_type =
							tribe.ware_index(ware_type_name);
						if (not ware_type)
							log
								("WARNING: target quantity configured for \"%s\", "
								 "which is not a ware type defined in tribe %s, "
								 "ignoring\n",
								 ware_type_name, tribe.name().c_str());
						else if
							(tribe.get_ware_descr(ware_type)->default_target_quantity()
							 ==
							 std::numeric_limits<uint32_t>::max())
							log
								("WARNING: target quantity configured for %s, which "
								 "should not have target quantity, ignoring\n",
								 ware_type_name);
						else {
							Target_Quantity & tq =
								m_target_quantities[ware_type.value()];
							if (tq.last_modified)
								throw wexception
									("duplicated entry for %s", ware_type_name);
							tq.permanent         = permanent;
							tq.temporary         = temporary;
							tq.last_modified     = last_modified;
						}
					}
				} catch (_wexception const & e) {
					throw wexception("target quantities: %s", e.what());
				}
			m_request_timerid = fr.Unsigned32();
		} else {
			throw wexception("unknown version %u", version);
		}
	} catch (std::exception const & e) {
		throw wexception("economy: %s", e.what());
	}
}

void Economy::Write(FileWrite & fw, Game &, Map_Map_Object_Saver *)
{
	fw.Unsigned16(CURRENT_ECONOMY_VERSION);
	Tribe_Descr const & tribe = owner().tribe();
	for (Ware_Index i = tribe.get_nrwares(); i.value();) {
		--i;
		Target_Quantity const & tq = m_target_quantities[i.value()];
		if (Time const last_modified = tq.last_modified) {
			fw.Unsigned32(last_modified);
			fw.CString(tribe.get_ware_descr(i)->name());
			fw.Unsigned32(tq.permanent);
			fw.Unsigned32(tq.temporary);
		}
	}
	fw.Unsigned32(0); //  terminator
	fw.Unsigned32(m_request_timerid);
}


Cmd_Call_Economy_Balance::Cmd_Call_Economy_Balance
	(int32_t const starttime, Economy * const economy, uint32_t const timerid)
	: GameLogicCommand(starttime)
{
	m_flag = &economy->get_arbitrary_flag();
	m_timerid = timerid;
}

/**
 * Called by Cmd_Queue as requested by start_request_timer().
 * Call economy functions to balance supply and request.
 */
void Cmd_Call_Economy_Balance::execute(Game & game)
{
	if (Flag * const flag = m_flag.get(game))
		flag->get_economy()->balance_requestsupply(m_timerid);
}

#define CURRENT_CMD_CALL_ECONOMY_VERSION 3

/**
 * Read and write
 */
void Cmd_Call_Economy_Balance::Read
	(FileRead & fr, Editor_Game_Base & egbase, Map_Map_Object_Loader & mol)
{
	try {
		uint16_t const packet_version = fr.Unsigned16();
		if (packet_version == CURRENT_CMD_CALL_ECONOMY_VERSION) {
			GameLogicCommand::Read(fr, egbase, mol);
			uint32_t serial = fr.Unsigned32();
			if (serial)
				m_flag = &mol.get<Flag>(serial);
			m_timerid = fr.Unsigned32();
		} else if (packet_version == 1 || packet_version == 2) {
			GameLogicCommand::Read(fr, egbase, mol);
			Player * const player = egbase.get_player(fr.Unsigned8());
			Economy * const economy =
				fr.Unsigned8 () ?
				player->get_economy_by_number(fr.Unsigned16()) : 0;
			m_flag = &economy->get_arbitrary_flag();
			if (packet_version >= 2)
				m_timerid = fr.Unsigned32();
			else
				m_timerid = 0;
		} else
			throw wexception("unknown/unhandled version %u", packet_version);
	} catch (_wexception const & e) {
		throw wexception("call economy balance: %s", e.what());
	}
}
void Cmd_Call_Economy_Balance::Write
	(FileWrite & fw, Editor_Game_Base & egbase, Map_Map_Object_Saver & mos)
{
	fw.Unsigned16(CURRENT_CMD_CALL_ECONOMY_VERSION);

	// Write Base Commands
	GameLogicCommand::Write(fw, egbase, mos);
	if (Flag * const flag = m_flag.get(egbase))
		fw.Unsigned32(mos.get_object_file_index(*flag));
	else
		fw.Unsigned32(0);
	fw.Unsigned32(m_timerid);
}

};
