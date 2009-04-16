/*
	------------------------------------------------------------------------------------
	LICENSE:
	------------------------------------------------------------------------------------
	This file is part of EVEmu: EVE Online Server Emulator
	Copyright 2006 - 2008 The EVEmu Team
	For the latest information visit http://evemu.mmoforge.org
	------------------------------------------------------------------------------------
	This program is free software; you can redistribute it and/or modify it under
	the terms of the GNU Lesser General Public License as published by the Free Software
	Foundation; either version 2 of the License, or (at your option) any later
	version.

	This program is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License along with
	this program; if not, write to the Free Software Foundation, Inc., 59 Temple
	Place - Suite 330, Boston, MA 02111-1307, USA, or go to
	http://www.gnu.org/copyleft/lesser.txt.
	------------------------------------------------------------------------------------
	Author:		Zhur
*/

#include "EvemuPCH.h"

#ifndef INT_MAX
#	define INT_MAX 0x7FFFFFFF
#endif

const uint32 SKILL_BASE_POINTS = 250;

/*
 * ItemData
 */
ItemData::ItemData(
	const char *_name,
	uint32 _typeID,
	uint32 _ownerID,
	uint32 _locationID,
	EVEItemFlags _flag,
	bool _contraband,
	bool _singleton,
	uint32 _quantity,
	const GPoint &_position,
	const char *_customInfo)
: name(_name),
  typeID(_typeID),
  ownerID(_ownerID),
  locationID(_locationID),
  flag(_flag),
  contraband(_contraband),
  singleton(_singleton),
  quantity(_quantity),
  position(_position),
  customInfo(_customInfo)
{
}

ItemData::ItemData(
	uint32 _typeID,
	uint32 _ownerID,
	uint32 _locationID,
	EVEItemFlags _flag,
	uint32 _quantity,
	const char *_customInfo,
	bool _contraband)
: name(""),
  typeID(_typeID),
  ownerID(_ownerID),
  locationID(_locationID),
  flag(_flag),
  contraband(_contraband),
  singleton(false),
  quantity(_quantity),
  position(0, 0, 0),
  customInfo(_customInfo)
{
}

ItemData::ItemData(
	uint32 _typeID,
	uint32 _ownerID,
	uint32 _locationID,
	EVEItemFlags _flag,
	const char *_name,
	const GPoint &_position,
	const char *_customInfo,
	bool _contraband)
: name(_name),
  typeID(_typeID),
  ownerID(_ownerID),
  locationID(_locationID),
  flag(_flag),
  contraband(_contraband),
  singleton(true),
  quantity(1),
  position(_position),
  customInfo(_customInfo)
{
}

/*
 * InventoryItem
 */
InventoryItem::InventoryItem(
	ItemFactory &_factory,
	uint32 _itemID,
	const Type &_type,
	const ItemData &_data)
: attributes(*this, &_factory.db(), &_factory.entity_list),
  m_refCount(1),
  m_factory(_factory),
  m_itemID(_itemID),
  m_itemName(_data.name),
  m_type(_type),
  m_ownerID(_data.ownerID),
  m_locationID(_data.locationID),
  m_flag(_data.flag),
  m_contraband(_data.contraband),
  m_singleton(_data.singleton),
  m_quantity(_data.quantity),
  m_position(_data.position),
  m_customInfo(_data.customInfo),
  m_contentsLoaded(false)
{
	// assert for data consistency
	assert(_data.typeID == _type.id());

	_log(ITEM__TRACE, "Created object %p for item %s (%lu).", this, itemName().c_str(), itemID());
}

InventoryItem::~InventoryItem() {
	if(m_refCount > 0) {
		_log(ITEM__ERROR, "Destructing an inventory item (%p) which has %d references!", this, m_refCount);
	}

	std::map<uint32, InventoryItem *>::iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		cur->second->Release();
	}
}

void InventoryItem::Release() {
	if(this == NULL) {
		_log(ITEM__ERROR, "Trying to release a NULL inventory item!");
		//TODO: log a stack.
		return;
	}
	if(m_refCount < 1) {
		_log(ITEM__ERROR, "Releasing an inventory item (%p) which has no references!", this);
		return;
	}
	m_refCount--;
	if(m_refCount == 0) {
		delete this;
	}
}

InventoryItem *InventoryItem::Ref() {
	if(this == NULL) {
		_log(ITEM__ERROR, "Trying to make a ref of a NULL inventory item!");
		//TODO: log a stack.
		return NULL;
	}
	if(m_refCount < 1) {
		_log(ITEM__ERROR, "Attempting to make a ref of inventory item (%p) which has no references at all!", this);
		return NULL;	//NULL is easier to debug than a bad pointer
	}
	m_refCount++;
	return(this);
}

InventoryItem *InventoryItem::Load(ItemFactory &factory, uint32 itemID, bool recurse) {
	// get the object
	InventoryItem *i = InventoryItem::_Load(factory, itemID);
	if(i == NULL)
		return NULL;

	// do the dynamic load
	if(!i->_Load(recurse)) {
		i->Release();	// should delete the item
		return NULL;
	}

	return(i);
}

InventoryItem *InventoryItem::_Load(ItemFactory &factory, uint32 itemID
) {
	// pull the item info
	ItemData data;
	if(!factory.db().GetItem(itemID, data))
		return NULL;

	// obtain type
	const Type *type = factory.GetType(data.typeID);
	if(type == NULL)
		return NULL;

	return(
		InventoryItem::_Load(factory, itemID, *type, data)
	);
}

InventoryItem *InventoryItem::_Load(ItemFactory &factory, uint32 itemID,
	// InventoryItem stuff:
	const Type &type, const ItemData &data
) {
	// construct actual object
	switch(type.categoryID()) {
		///////////////////////////////////////
		// Blueprint:
		///////////////////////////////////////
		case EVEDB::invCategories::Blueprint: {
			// cast the type into what it really is ...
			const BlueprintType &bpType = static_cast<const BlueprintType &>(type);

			// create the blueprint
			return(BlueprintItem::_Load(
				factory, itemID, bpType, data
			));
		};

		///////////////////////////////////////
		// Default:
		///////////////////////////////////////
		default: {
			// we are generic item; create one
			return(new InventoryItem(
				factory, itemID, type, data
			));
		};
	}
}

bool InventoryItem::_Load(bool recurse) {
	// load attributes
	if(!attributes.Load())
		return false;

	// update container
	InventoryItem *container = m_factory._GetIfContentsLoaded(locationID());
	if(container != NULL) {
		container->AddContainedItem(this);
		container->Release();
	}

	// now load contained items
	if(recurse) {
		if(!LoadContents(recurse))
			return false;
	}

	return true;
}

InventoryItem *InventoryItem::Spawn(ItemFactory &factory, ItemData &data) {
	// obtain type of new item
	const Type *t = factory.GetType(data.typeID);
	if(t == NULL)
		return NULL;

	// fix the name
	if(data.name.empty())
		data.name = t->name();

	switch(t->categoryID()) {
		///////////////////////////////////////
		// Blueprint:
		///////////////////////////////////////
		case EVEDB::invCategories::Blueprint: {
			BlueprintData bdata; // use default blueprint attributes

			return(BlueprintItem::Spawn(
				factory, data, bdata
			));
		}

		///////////////////////////////////////
		// Default:
		///////////////////////////////////////
		default: {
			return(InventoryItem::_Spawn(
				factory, data
			));
		};
	}
}

InventoryItem *InventoryItem::_Spawn(ItemFactory &factory,
	// InventoryItem stuff:
	const ItemData &data
) {
	// insert new entry into DB
	uint32 itemID = factory.db().NewItem(data);
	if(itemID == 0)
		return NULL;

	// load the item
	// recurse is useless since item cannot contain anything yet
	return(
		InventoryItem::Load(factory, itemID, false)
	);
}

bool InventoryItem::LoadContents(bool recursive) {
	if(m_contentsLoaded)
		return true;

	_log(ITEM__TRACE, "Recursively loading contents of cached item %lu", m_itemID);

	//load the list of items we need
	std::vector<uint32> m_itemIDs;
	if(!m_factory.db().GetItemContents(itemID(), m_itemIDs))
		return false;
	
	//Now get each one from the factory (possibly recursing)
	std::vector<uint32>::iterator cur, end;
	cur = m_itemIDs.begin();
	end = m_itemIDs.end();
	for(; cur != end; cur++) {
		InventoryItem *i = m_factory.GetItem(*cur, recursive);
		if(i == NULL) {
			_log(ITEM__ERROR, "Failed to load item %lu contained in %lu. Skipping.", *cur, m_itemID);
			continue;
		}

		AddContainedItem(i);
		i->Release();
	}

	m_contentsLoaded = true;
	return true;
}

void InventoryItem::Save(bool recursive, bool saveAttributes) const {
	_log(ITEM__TRACE, "Saving item %lu.", m_itemID);

	m_factory.db().SaveItem(
		itemID(),
		ItemData(
			itemName().c_str(),
			typeID(),
			ownerID(),
			locationID(),
			flag(),
			contraband(),
			singleton(),
			quantity(),
			position(),
			customInfo().c_str()
		)
	);

	if(saveAttributes)
		attributes.Save();
	
	if(recursive) {
		std::map<uint32, InventoryItem *>::const_iterator cur, end;
		cur = m_contents.begin();
		end = m_contents.end();
		for(; cur != end; cur++) {
			InventoryItem *i = cur->second;

			i->Save(true);
		}
	}
}

void InventoryItem::Delete() {
	//first, get out of client's sight.
	//this also removes us from our container.
	Move(6);
	ChangeOwner(2);

	//we need to delete anything that we contain. This will be recursive.
	LoadContents(true);

	std::map<uint32, InventoryItem *>::iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; ) {
		InventoryItem *i = cur->second->Ref();
		//iterator will become invalid after calling
		//Delete(), so increment now.
		cur++;

		i->Delete();
	}
	//Delete() releases our ref on these.
	//so it should be empty anyway.
	m_contents.clear();

	//now we need to tell the factory to get rid of us from its cache.
	m_factory._DeleteItem(m_itemID);
	
	//now we take ourself out of the DB
	attributes.Delete();
	m_factory.db().DeleteItem(m_itemID);
	
	//and now we destroy ourself.
	if(m_refCount != 1) {
		_log(ITEM__ERROR, "Delete() called on item %lu (%p) which has %d references! Invalidating as best as possible..", m_itemID, this, m_refCount);
		m_itemName = "BAD DELETED ITEM";
		m_quantity = 0;
		m_contentsLoaded = true;
		//TODO: mark the item for death so that if it does get Release()'d, it will properly die.
	}
	Release();
	//we should be deleted now.... so dont do anything!
}

PyRepObject *InventoryItem::GetEntityRow() const {
	EntityRowObject ero;
	ero.itemID = m_itemID;
	ero.typeID = typeID();
	ero.ownerID = m_ownerID;
	ero.locationID = m_locationID;
	ero.flag = m_flag;
	ero.contraband = m_contraband?1:0;
	ero.singleton = m_singleton?1:0;
	ero.quantity = m_quantity;
	ero.groupID = groupID();
	ero.categoryID = categoryID();
	ero.customInfo = m_customInfo;
	return(ero.Encode());
}

PyRepObject *InventoryItem::GetInventoryRowset(EVEItemFlags _flag, uint32 forOwner) const {
	util_Rowset rowset;
	
	rowset.rowclass = "util.Row";
	
	rowset.header.push_back("itemID");
	rowset.header.push_back("typeID");
	rowset.header.push_back("ownerID");
	rowset.header.push_back("locationID");
	rowset.header.push_back("flag");
	rowset.header.push_back("contraband");
	rowset.header.push_back("singleton");
	rowset.header.push_back("quantity");
	rowset.header.push_back("groupID");
	rowset.header.push_back("categoryID");
	rowset.header.push_back("customInfo");
	
	//there has to be a better way to build this...
	InventoryItem *i;
	std::map<uint32, InventoryItem *>::const_iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		i = cur->second;
		if((_flag == flagAnywhere || i->flag() == _flag) &&
			(forOwner == NULL || i->ownerID() == forOwner))
		{
			PyRepList *item = new PyRepList();
			item->addInt(i->itemID());
			item->addInt(i->typeID());
			item->addInt(i->ownerID());
			item->addInt(i->locationID());
			item->addInt(i->flag());
			item->addInt(i->contraband());
			item->addInt(i->singleton());
			item->addInt(i->quantity());
			item->addInt(i->groupID());
			item->addInt(i->categoryID());
			if(i->customInfo().empty()) {
				item->add(new PyRepNone());
			} else {
				item->addStr(i->customInfo().c_str());
			}
			rowset.lines.add(item);
		}
	}
	
	return(rowset.FastEncode());
}

bool InventoryItem::Populate(Rsp_CommonGetInfo_Entry &result) const {
	//itemID:
	result.itemID = m_itemID;

	//invItem:
	result.invItem = GetEntityRow();
	if(result.invItem == NULL) {
		codelog(ITEM__ERROR, "%s (%lu): Unable to build item row for move", m_itemName.c_str(), m_itemID);
		return false;
	}

	//hacky, but it doesn't really hurt anything.
	if(isOnline() != 0) {
		//there is an effect that goes along with this. We should
		//probably be properly tracking the effect due to some
		// timer things, but for now, were hacking it.
		EntityEffectState es;
		es.env_itemID = m_itemID;
		es.env_charID = m_ownerID;	//may not be quite right...
		es.env_shipID = m_locationID;
		es.env_target = m_locationID;	//this is what they do.
		es.env_other = new PyRepNone;
		es.env_effectID = effectOnline;
		es.startTime = Win32TimeNow() - Win32Time_Hour;	//act like it happened an hour ago
		es.duration = INT_MAX;
		es.repeat = 0;
		es.randomSeed = new PyRepNone;

		result.activeEffects[es.env_effectID] = es.FastEncode();
	}
	
	//activeEffects:
	//result.entry.activeEffects[id] = List[11];
	
	//attributes:
	attributes.EncodeAttributes(result.attributes);
	
	//no idea what time this is supposed to be
	//codelog(ITEM__WARNING, "%s (%lu): sending faked time", m_itemName.c_str(), m_itemID);
	result.time = Win32TimeNow();

	return true;
}

PyRepObject *InventoryItem::ItemGetInfo() const {
	Rsp_ItemGetInfo result;

	if(!Populate(result.entry))
		return NULL;	//print already done.
	
	return(result.FastEncode());
}

PyRepObject *InventoryItem::ShipGetInfo() {
	//TODO: verify that we are a ship?
	
	if(!LoadContents(true)) {
		codelog(ITEM__ERROR, "%s (%lu): Failed to load contents for ShipGetInfo", m_itemName.c_str(), m_itemID);
		return NULL;
	}

	Rsp_CommonGetInfo result;
	Rsp_CommonGetInfo_Entry entry;

	//first populate the ship.
	if(!Populate(entry))
		return NULL;	//print already done.
	
//hacking:
	//maximumRangeCap
		entry.attributes[797] = new PyRepReal(250000.000000);
	
	result.items[m_itemID] = entry.FastEncode();

	//now encode contents...
	std::vector<InventoryItem *> equipped;
	//find all the equipped items
	FindByFlagRange(flagLowSlot0, flagFixedSlot, equipped, false);
	//encode an entry for each one.
	std::vector<InventoryItem *>::iterator cur, end;
	cur = equipped.begin();
	end = equipped.end();
	for(; cur != end; cur++) {
		if(!(*cur)->Populate(entry)) {
			codelog(ITEM__ERROR, "%s (%lu): Failed to load item %ld for ShipGetInfo", m_itemName.c_str(), m_itemID, (*cur)->m_itemID);
		} else {
			result.items[(*cur)->m_itemID] = entry.FastEncode();
		}
	}
	
	return(result.FastEncode());
}

PyRepObject *InventoryItem::CharGetInfo() {
	//TODO: verify that we are a char?
	
	if(!LoadContents(true)) {
		codelog(ITEM__ERROR, "%s (%lu): Failed to load contents for CharGetInfo", m_itemName.c_str(), m_itemID);
		return NULL;
	}

	Rsp_CommonGetInfo result;
	Rsp_CommonGetInfo_Entry entry;

	//first encode self.
	if(!Populate(entry))
		return NULL;	//print already done.
	
	result.items[m_itemID] = entry.FastEncode();

	//now encode skills...
	std::vector<InventoryItem *> skills;
	//find all the skills contained within ourself.
	FindByFlag(flagSkill, skills, false);
	//encode an entry for each one.
	std::vector<InventoryItem *>::iterator cur, end;
	cur = skills.begin();
	end = skills.end();
	for(; cur != end; cur++) {
		if(!(*cur)->Populate(entry)) {
			codelog(ITEM__ERROR, "%s (%lu): Failed to load skill item %ld for CharGetInfo", m_itemName.c_str(), m_itemID, (*cur)->m_itemID);
		} else {
			result.items[(*cur)->m_itemID] = entry.FastEncode();
		}
	}
	
	return(result.FastEncode());
}


InventoryItem *InventoryItem::FindFirstByFlag(EVEItemFlags _flag, bool newref) {
	std::map<uint32, InventoryItem *>::iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		InventoryItem *i = cur->second;

		if(i->m_flag == _flag) {
			return(newref ? i->Ref() : i);
		}
	}

	return NULL;
}

InventoryItem *InventoryItem::GetByID(uint32 id, bool newref) {
	std::map<uint32, InventoryItem *>::iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		InventoryItem *i = cur->second;

		if(i->m_itemID == id) {
			return(newref ? i->Ref() : i);
		}
	}

	return NULL;
}

uint32 InventoryItem::FindByFlag(EVEItemFlags _flag, std::vector<InventoryItem *> &items, bool newref) {
	uint32 count = 0;

	std::map<uint32, InventoryItem *>::const_iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		InventoryItem *i = cur->second;

		if(i->m_flag == _flag) {
			items.push_back(newref ? i->Ref() : i);
			count++;
		}
	}

	return(count);
}

uint32 InventoryItem::FindByFlagRange(EVEItemFlags low_flag, EVEItemFlags high_flag, std::vector<InventoryItem *> &items, bool newref) {
	uint32 count = 0;

	std::map<uint32, InventoryItem *>::const_iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		InventoryItem *i = cur->second;

		if(i->m_flag >= low_flag && i->m_flag <= high_flag) {
			items.push_back(newref ? i->Ref() : i);
			count++;
		}
	}

	return(count);
}

uint32 InventoryItem::FindByFlagSet(std::set<EVEItemFlags> flags, std::vector<InventoryItem *> &items, bool newref) {
	uint32 count = 0;

	std::map<uint32, InventoryItem *>::const_iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		InventoryItem *i = cur->second;

		if(flags.find(i->flag()) != flags.end()) {
			items.push_back(newref ? i->Ref() : i);
			count++;
		}
	}

	return(count);
}

void InventoryItem::AddContainedItem(InventoryItem *it) {
	std::map<uint32, InventoryItem *>::iterator res;
	res = m_contents.find(it->itemID());
	if(res == m_contents.end()) {
		m_contents[it->itemID()] = it->Ref();

		_log(ITEM__TRACE, "   Updated location %lu to contain item %lu", itemID(), it->itemID());
	} else if(res->second != it) {
		_log(ITEM__ERROR, "Both object %p and object %p represent item %lu!", res->second, it, it->itemID());
	} //else already here
}

void InventoryItem::RemoveContainedItem(InventoryItem *it) {
	std::map<uint32, InventoryItem *>::iterator old_inst;
	old_inst = m_contents.find(it->itemID());
	if(old_inst != m_contents.end()) {
		old_inst->second->Release();
		m_contents.erase(old_inst);

		_log(ITEM__TRACE, "   Updated location %lu to no longer contain item %lu", itemID(), it->itemID());
	}
}

void InventoryItem::Rename(const char *to) {
	m_itemName = to;
	// TODO: send some kind of update?
	Save(false, false);
}

void InventoryItem::MoveInto(InventoryItem *new_home, EVEItemFlags _flag, bool notify) {
	Move(new_home->m_itemID, _flag, notify);
}

void InventoryItem::Move(uint32 location, EVEItemFlags new_flag, bool notify) {
	uint32 old_location = m_locationID;
	EVEItemFlags old_flag = m_flag;
	
	if(location == old_location && new_flag == old_flag)
		return;	//nothing to do...
	
	//first, take myself out of my old container, if its loaded.
	InventoryItem *old_container = m_factory._GetIfContentsLoaded(old_location);
	if(old_container != NULL) {
		old_container->RemoveContainedItem(this);	//releases its ref
		old_container->Release();
	}
	
	//then make sure that my new container is updated, if its loaded.
	InventoryItem *new_container = m_factory._GetIfContentsLoaded(location);
	if(new_container != NULL) {
		new_container->AddContainedItem(this);	//makes a new ref
		new_container->Release();
	}

	m_locationID = location;
	m_flag = new_flag;

	Save(false, false);

	//notify about the changes.
	if(notify) {
		std::map<uint32, PyRep *> changes;
		changes[ixLocationID] = new PyRepInteger(old_location);
		if(new_flag != old_flag)
			changes[ixFlag] = new PyRepInteger(old_flag);
		SendItemChange(m_ownerID, changes);	//changes is consumed
	}
}

void InventoryItem::ChangeFlag(EVEItemFlags new_flag, bool notify) {
	EVEItemFlags old_flag = m_flag;

	if(new_flag == old_flag)
		return;	//nothing to do...

	m_flag = new_flag;

	Save(false, false);

	//notify about the changes.
	if(notify) {
		std::map<uint32, PyRep *> changes;
		changes[ixFlag] = new PyRepInteger(old_flag);
		SendItemChange(m_ownerID, changes);	//changes is consumed
	}
}

bool InventoryItem::AlterQuantity(int32 qty_change, bool notify) {
	if(qty_change == 0)
		return true;

	int32 new_qty = m_quantity + qty_change;

	if(new_qty < 0) {
		codelog(ITEM__ERROR, "%s (%lu): Tried to remove %ld quantity from stack of %lu", m_itemName.c_str(), m_itemID, -qty_change, m_quantity);
		return false;
	}

	return(SetQuantity(new_qty, notify));
}

bool InventoryItem::SetQuantity(uint32 qty_new, bool notify) {
	//if an object has its singleton set then it shouldn't be able to add/remove qty
	if(m_singleton) {
		//Print error
		codelog(ITEM__ERROR, "%s (%lu): Failed to set quantity %lu , the items singleton bit is set", m_itemName.c_str(), m_itemID, qty_new);
		//return false
		return false;
	}

	uint32 old_qty = m_quantity;
	uint32 new_qty = qty_new;
	
	m_quantity = new_qty;

	Save(false, false);

	//notify about the changes.
	if(notify) {
		std::map<uint32, PyRep *> changes;

		//send the notify to the new owner.
		changes[ixQuantity] = new PyRepInteger(old_qty);
		SendItemChange(m_ownerID, changes);	//changes is consumed
	}
	
	return true;
}

InventoryItem *InventoryItem::Split(int32 qty_to_take, bool notify) {
	if(qty_to_take <= 0) {
		_log(ITEM__ERROR, "%s (%lu): Asked to split into a chunk of %ld", itemName().c_str(), itemID(), qty_to_take);
		return NULL;
	}
	if(!AlterQuantity(-qty_to_take, notify)) {
		_log(ITEM__ERROR, "%s (%lu): Failed to remove quantity %ld during split.", itemName().c_str(), itemID(), qty_to_take);
		return NULL;
	}

	ItemData idata(
		typeID(),
		ownerID(),
		(notify ? 1 : locationID()), //temp location to cause the spawn via update
		flag(),
		qty_to_take
	);

	InventoryItem *res = m_factory.SpawnItem(idata);
	if(notify)
		res->Move(locationID(), flag());

	return( res );
}

bool InventoryItem::Merge(InventoryItem *to_merge, int32 qty, bool notify) {
	if(to_merge == NULL) {
		_log(ITEM__ERROR, "%s (%lu): Cannot merge with NULL item.", itemName().c_str(), itemID());
		return false;
	}
	if(typeID() != to_merge->typeID()) {
		_log(ITEM__ERROR, "%s (%lu): Asked to merge with %s (%lu).", itemName().c_str(), itemID(), to_merge->itemName().c_str(), to_merge->itemID());
		return false;
	}
	if(locationID() != to_merge->locationID() || flag() != to_merge->flag()) {
		_log(ITEM__ERROR, "%s (%lu) in location %lu, flag %lu: Asked to merge with item %lu in location %lu, flag %lu.", itemName().c_str(), itemID(), locationID(), flag(), to_merge->itemID(), to_merge->locationID(), to_merge->flag());
		return false;
	}
	if(qty == 0)
		qty = to_merge->quantity();
	if(qty <= 0) {
		_log(ITEM__ERROR, "%s (%lu): Asked to merge with %ld units of item %lu.", itemName().c_str(), itemID(), qty, to_merge->itemID());
		return false;
	}
	if(!AlterQuantity(qty, notify)) {
		_log(ITEM__ERROR, "%s (%lu): Failed to add quantity %ld.", itemName().c_str(), itemID(), qty);
		return false;
	}

	if(qty == to_merge->quantity())
		to_merge->Delete();	//consumes ref
	else if(!to_merge->AlterQuantity(-qty, notify)) {
		_log(ITEM__ERROR, "%s (%lu): Failed to remove quantity %ld.", to_merge->itemName().c_str(), to_merge->itemID(), qty);
		return false;
	} else
		to_merge->Release();	//consume ref

	return true;
}

bool InventoryItem::ChangeSingleton(bool new_singleton, bool notify) {
	bool old_singleton = m_singleton;
	
	if(new_singleton == old_singleton)
		return true;	//nothing to do...

	m_singleton = new_singleton;

	Save(false, false);

	//notify about the changes.
	if(notify) {
		std::map<uint32, PyRep *> changes;
		changes[ixSingleton] = new PyRepInteger(old_singleton);
		SendItemChange(m_ownerID, changes);	//changes is consumed
	}
	return true;
}

void InventoryItem::ChangeOwner(uint32 new_owner, bool notify) {
	uint32 old_owner = m_ownerID;
	
	if(new_owner == old_owner)
		return;	//nothing to do...
	
	m_ownerID = new_owner;

	Save(false, false);
	
	//notify about the changes.
	if(notify) {
		std::map<uint32, PyRep *> changes;

		//send the notify to the new owner.
		changes[ixOwnerID] = new PyRepInteger(old_owner);
		SendItemChange(new_owner, changes);	//changes is consumed

		//also send the notify to the old owner.
		changes[ixOwnerID] = new PyRepInteger(old_owner);
		SendItemChange(old_owner, changes);	//changes is consumed
	}
}

//contents of changes are consumed and cleared
void InventoryItem::SendItemChange(uint32 toID, std::map<uint32, PyRep *> &changes) const {
	//TODO: figure out the appropriate list of interested people...
	Client *c = m_factory.entity_list.FindCharacter(toID);
	if(c == NULL)
		return;	//not found or not online...
	
	
	NotifyOnItemChange change;
	change.itemRow = GetEntityRow();
	if(change.itemRow == NULL) {
		codelog(ITEM__ERROR, "%s (%lu): Unable to build item row for move", m_itemName.c_str(), m_itemID);
		return;
	}
	change.changes = changes;
	changes.clear();	//consume them.

	PyRepTuple *tmp = change.Encode();	//this is consumed below
	c->SendNotification("OnItemChange", "charid", &tmp, false);	//unsequenced.
}

void InventoryItem::PutOnline() {
	SetOnline(true);
}

void InventoryItem::PutOffline() {
	SetOnline(false);
}

void InventoryItem::SetOnline(bool newval) {
	//bool old = isOnline();
	Set_isOnline(newval);
	
	Client *c = m_factory.entity_list.FindCharacter(m_ownerID);
	if(c == NULL)
		return;	//not found or not online...
	
	Notify_OnModuleAttributeChange omac;
	omac.ownerID = m_ownerID;
	omac.itemKey = m_itemID;
	omac.attributeID = ItemAttributeMgr::Attr_isOnline;
	omac.time = Win32TimeNow();
	omac.newValue = new PyRepInteger(newval?1:0);
	omac.oldValue = new PyRepInteger(newval?0:1);	//hack... should use old, but its not cooperating today.
	
	Notify_OnGodmaShipEffect ogf;
	ogf.itemID = m_itemID;
	ogf.effectID = effectOnline;
	ogf.when = Win32TimeNow();
	ogf.start = newval?1:0;
	ogf.active = newval?1:0;
	ogf.env_itemID = ogf.itemID;
	ogf.env_charID = m_ownerID;	//??
	ogf.env_shipID = m_locationID;
	ogf.env_target = m_locationID;
	ogf.env_effectID = ogf.effectID;
	ogf.startTime = ogf.when;
	ogf.duration = INT_MAX;	//I think this should be infinity (0x07 may be infinity?)
	ogf.repeat = 0;
	ogf.randomSeed = new PyRepNone();
	ogf.error = new PyRepNone();
	
	Notify_OnMultiEvent multi;
	multi.events.add(omac.FastEncode());
	multi.events.add(ogf.FastEncode());
	
	PyRepTuple *tmp = multi.FastEncode();	//this is consumed below
	c->SendNotification("OnMultiEvent", "clientID", &tmp);
	
	
}

void InventoryItem::SetCustomInfo(const char *ci) {
	if(ci == NULL)
		m_customInfo = "";
	else
		m_customInfo = ci;
	Save(false, false);
}

bool InventoryItem::Contains(InventoryItem *item, bool recursive) const {
	if(m_contents.find(item->itemID()) != m_contents.end())
		return true;
	if(!recursive)
		return false;
	InventoryItem *i;
	std::map<uint32, InventoryItem *>::const_iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; cur++) {
		i = cur->second;
		if(i->Contains(item, true))
			return true;
	}
	return false;
}

//I think I ultimately want this logic somewhere else...
//this doesn't seem to be working either... for some reason, the client is
//getting 0 for its skill points per minute field and throwing a division
// exception.
void InventoryItem::TrainSkill(InventoryItem *skill) {
	if(m_flag != flagPilot) {
		codelog(ITEM__ERROR, "%s (%lu): Tried to train skill %lu on non-pilot object.", m_itemName.c_str(), m_itemID, skill->itemID());
		return;
	}
	
	if(skill->flag() == flagSkillInTraining) {
		_log(ITEM__TRACE, "%s (%lu): Requested to train skill %lu item %lu but it is already in training. Doing nothing.", m_itemName.c_str(), m_itemID, skill->typeID(), skill->itemID());
		return;
	}
	
	Client *c = m_factory.entity_list.FindCharacter(m_ownerID);
	
	//stop training our old skill...
	//search for all, just in case we screwed up
	std::vector<InventoryItem *> skills;
	//find all the skills contained within ourself.
	FindByFlag(flagSkillInTraining, skills, false);
	//encode an entry for each one.
	std::vector<InventoryItem *>::iterator cur, end;
	cur = skills.begin();
	end = skills.end();
	for(; cur != end; cur++) {
		InventoryItem *i = *cur;
		i->ChangeFlag(flagSkill);
		
		if(c != NULL) {
			NotifyOnSkillTrainingStopped osst;
			osst.itemID = skill->itemID();
			osst.endOfTraining = 0;
			PyRepTuple *tmp = osst.FastEncode();	//this is consumed below
			c->SendNotification("OnSkillTrainingStopped", "charid", &tmp);
		}
	}

	if(skill->flag() != flagSkill) {
		//this is a skill book being trained for the first time.
		_log(ITEM__TRACE, "%s (%lu): Initial training of skill %lu item %lu", m_itemName.c_str(), m_itemID, skill->typeID(), skill->itemID());
		skill->MoveInto(this, flagSkillInTraining);
		//set the initial attributes.
		skill->Set_skillLevel(0);
		skill->Set_skillPoints(0);
	} else if(!Contains(skill)) {
		//this must be a skill in another container...
		_log(ITEM__ERROR, "%s (%lu): Tried to train skill %lu item %lu which has the skill flag but is not contained within this item.", m_itemName.c_str(), m_itemID, skill->itemID());
		return;
	} else {
		//skill is within this item, change its flag.
		_log(ITEM__TRACE, "%s (%lu): Starting training of skill %lu item %lu", m_itemName.c_str(), m_itemID, skill->typeID(), skill->itemID());
		skill->ChangeFlag(flagSkillInTraining, true);
	}
	
	if(c != NULL) {
		NotifyOnSkillStartTraining osst;
		osst.itemID = skill->itemID();
		osst.endOfTraining = Win32TimeNow() + Win32Time_Month; //hack hack hack
		
		PyRepTuple *tmp = osst.FastEncode();	//this is consumed below
		c->SendNotification("OnSkillStartTraining", "charid", &tmp);
	}
}

void InventoryItem::Relocate(const GPoint &pos) {
	if(m_position == pos)
		return;

	m_position = pos;

	Save(false, false);
}

void InventoryItem::StackContainedItems(EVEItemFlags locFlag, uint32 forOwner) {
	std::map<uint32, InventoryItem *> types;

	InventoryItem *i;
	std::map<uint32, InventoryItem *>::iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	for(; cur != end; ) {
		i = cur->second;
		cur++;

		if(	   !i->singleton()
			&& (forOwner == i->ownerID() || forOwner == NULL)
		) {
			std::map<uint32, InventoryItem *>::iterator res = types.find(i->typeID());
			if(res == types.end())
				types[i->typeID()] = i;
			else
				//dont forget to make ref (which is consumed)
				res->second->Merge(i->Ref());
		}
	}
}

double InventoryItem::GetRemainingCapacity(EVEItemFlags locationFlag) const {
	double remainingCargoSpace;
	//Get correct initial cargo value
	//TODO: Deal with cargo space bonuses
	switch (locationFlag) {
		case flagCargoHold: remainingCargoSpace = capacity(); break;
		case flagDroneBay:	remainingCargoSpace = droneCapacity(); break;
		default:			remainingCargoSpace = 0.0; break;
	}
	
	//TODO: And implement Sizes for packaged ships
 	std::map<uint32, InventoryItem *>::const_iterator cur, end;
	cur = m_contents.begin();
	end = m_contents.end();
	
	for(; cur != end; cur++) {
		if(cur->second->flag() == locationFlag)
			remainingCargoSpace -= (cur->second->quantity() * cur->second->volume());
	}

	return(remainingCargoSpace);
}

/*
 * BlueprintData
 */
BlueprintData::BlueprintData(
	bool _copy,
	uint32 _materialLevel,
	uint32 _productivityLevel,
	int32 _licensedProductionRunsRemaining)
: copy(_copy),
  materialLevel(_materialLevel),
  productivityLevel(_productivityLevel),
  licensedProductionRunsRemaining(_licensedProductionRunsRemaining)
{
}

/*
 * BlueprintItem
 */
BlueprintItem::BlueprintItem(
	ItemFactory &_factory,
	uint32 _blueprintID,
	// InventoryItem stuff:
	const BlueprintType &_bpType,
	const ItemData &_data,
	// BlueprintItem stuff:
	const BlueprintData &_bpData)
: InventoryItem(_factory, _blueprintID, _bpType, _data),
  m_copy(_bpData.copy),
  m_materialLevel(_bpData.materialLevel),
  m_productivityLevel(_bpData.productivityLevel),
  m_licensedProductionRunsRemaining(_bpData.licensedProductionRunsRemaining)
{
	// data consistency asserts
	assert(_bpType.categoryID() == EVEDB::invCategories::Blueprint);
}

BlueprintItem *BlueprintItem::Load(ItemFactory &factory, uint32 blueprintID, bool recurse) {
	BlueprintItem *bi = BlueprintItem::_Load(factory, blueprintID);
	if(bi == NULL)
		return NULL;

	// finish load
	if(!bi->_Load(recurse)) {
		bi->Release();	// should delete the item
		return NULL;
	}

	// return
	return(bi);
}

BlueprintItem *BlueprintItem::_Load(ItemFactory &factory, uint32 blueprintID
) {
	// pull the item info
	ItemData data;
	if(!factory.db().GetItem(blueprintID, data))
		return NULL;

	// obtain type
	const BlueprintType *bpType = factory.GetBlueprintType(data.typeID);
	if(bpType == NULL)
		return NULL;

	// Since we have successfully got the blueprint type it's really a blueprint,
	// so there's no need to check it.

	return(
		BlueprintItem::_Load(factory, blueprintID, *bpType, data)
	);
}

BlueprintItem *BlueprintItem::_Load(ItemFactory &factory, uint32 blueprintID,
	// InventoryItem stuff:
	const BlueprintType &bpType, const ItemData &data
) {
	// we are blueprint; pull additional blueprint info
	BlueprintData bpData;
	if(!factory.db().GetBlueprint(blueprintID, bpData))
		return NULL;

	return(
		BlueprintItem::_Load(factory, blueprintID, bpType, data, bpData)
	);
}

BlueprintItem *BlueprintItem::_Load(ItemFactory &factory, uint32 blueprintID,
	// InventoryItem stuff:
	const BlueprintType &bpType, const ItemData &data,
	// BlueprintItem stuff:
	const BlueprintData &bpData
) {
	// we have enough data, construct the item
	return(new BlueprintItem(
		factory, blueprintID, bpType, data, bpData
	));
}

bool BlueprintItem::_Load(bool recurse) {
	// redirect to parent
	return(InventoryItem::_Load(recurse));
}

BlueprintItem *BlueprintItem::Spawn(ItemFactory &factory, ItemData &data, BlueprintData &bpData) {
	const Type *t = factory.GetType(data.typeID);
	if(t == NULL)
		return NULL;

	// fix the name
	if(data.name.empty())
		data.name = t->name();

	// check if it's really a blueprint
	if(t->categoryID() != EVEDB::invCategories::Blueprint) {
		_log(ITEM__ERROR, "Trying to create blueprint with type %s.", t->name().c_str());
		return NULL;
	}

	return(
		BlueprintItem::_Spawn(factory, data, bpData)
	);
}

BlueprintItem *BlueprintItem::_Spawn(ItemFactory &factory,
	// InventoryItem stuff:
	const ItemData &data,
	// BlueprintItem stuff:
	const BlueprintData &bpData
) {
	// insert new entry into DB
	uint32 blueprintID = factory.db().NewItem(data);
	if(blueprintID == 0)
		return NULL;

	// insert blueprint entry into DB
	if(!factory.db().NewBlueprint(blueprintID, bpData))
		return NULL;

	return(
		BlueprintItem::Load(factory, blueprintID)
	);
}

void BlueprintItem::Save(bool recursive, bool saveAttributes) const {
	// save our parent
	InventoryItem::Save(recursive, saveAttributes);

	// save ourselves
	m_factory.db().SaveBlueprint(
		itemID(),
		BlueprintData(
			copy(),
			materialLevel(),
			productivityLevel(),
			licensedProductionRunsRemaining()
		)
	);
}

void BlueprintItem::Delete() {
	// delete our blueprint record
	m_factory.db().DeleteBlueprint(m_itemID);
	// redirect to parent
	InventoryItem::Delete();
}

BlueprintItem *BlueprintItem::SplitBlueprint(int32 qty_to_take, bool notify) {
	// split item
	BlueprintItem *res = static_cast<BlueprintItem *>(
		InventoryItem::Split(qty_to_take, notify)
	);
	if(res == NULL)
		return NULL;

	// copy our attributes
	res->SetCopy(m_copy);
	res->SetMaterialLevel(m_materialLevel);
	res->SetProductivityLevel(m_productivityLevel);
	res->SetLicensedProductionRunsRemaining(m_licensedProductionRunsRemaining);

	return(res);
}

bool BlueprintItem::Merge(InventoryItem *to_merge, int32 qty, bool notify) {
	if(!InventoryItem::Merge(to_merge, qty, notify))
		return false;
	// do something special? merge material level etc.?
	return true;
}

void BlueprintItem::SetCopy(bool copy) {
	m_copy = copy;

	Save(false, false);
}

void BlueprintItem::SetMaterialLevel(uint32 materialLevel) {
	m_materialLevel = materialLevel;

	Save(false, false);
}

bool BlueprintItem::AlterMaterialLevel(int32 materialLevelChange) {
	int32 new_material_level = m_materialLevel + materialLevelChange;

	if(new_material_level < 0) {
		_log(ITEM__ERROR, "%s (%lu): Tried to remove %lu material levels while having %lu levels.", m_itemName.c_str(), m_itemID, -materialLevelChange, m_materialLevel);
		return false;
	}

	SetMaterialLevel(new_material_level);
	return true;
}

void BlueprintItem::SetProductivityLevel(uint32 productivityLevel) {
	m_productivityLevel = productivityLevel;

	Save(false, false);
}

bool BlueprintItem::AlterProductivityLevel(int32 producitvityLevelChange) {
	int32 new_productivity_level = m_productivityLevel + producitvityLevelChange;

	if(new_productivity_level < 0) {
		_log(ITEM__ERROR, "%s (%lu): Tried to remove %lu productivity levels while having %lu levels.", m_itemName.c_str(), m_itemID, -producitvityLevelChange, m_productivityLevel);
		return false;
	}

	SetProductivityLevel(new_productivity_level);
	return true;
}

void BlueprintItem::SetLicensedProductionRunsRemaining(int32 licensedProductionRunsRemaining) {
	m_licensedProductionRunsRemaining = licensedProductionRunsRemaining;

	Save(false, false);
}

void BlueprintItem::AlterLicensedProductionRunsRemaining(int32 licensedProductionRunsRemainingChange) {
	int32 new_licensed_production_runs_remaining = m_licensedProductionRunsRemaining + licensedProductionRunsRemainingChange;

	SetLicensedProductionRunsRemaining(new_licensed_production_runs_remaining);
}

PyRepDict *BlueprintItem::GetBlueprintAttributes() const {
	Rsp_GetBlueprintAttributes rsp;

	// fill in our attribute info
	rsp.blueprintID = itemID();
	rsp.copy = copy() ? 1 : 0;
	rsp.productivityLevel = productivityLevel();
	rsp.materialLevel = materialLevel();
	rsp.licensedProductionRunsRemaining = licensedProductionRunsRemaining();
	rsp.wastageFactor = wasteFactor();

	rsp.productTypeID = productTypeID();
	rsp.manufacturingTime = type().productionTime();
	rsp.maxProductionLimit = type().maxProductionLimit();
	rsp.researchMaterialTime = type().researchMaterialTime();
	rsp.researchTechTime = type().researchTechTime();
	rsp.researchProductivityTime = type().researchProductivityTime();
	rsp.researchCopyTime = type().researchCopyTime();

	return(rsp.FastEncode());
}
