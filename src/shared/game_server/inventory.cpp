
#include "inventory.h"

#include "game_bag_s.h"
#include "game_item_s.h"
#include "game_player_s.h"
#include "base/linear_set.h"
#include "binary_io/reader.h"
#include "binary_io/vector_sink.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "shared/proto_data/items.pb.h"

namespace mmo
{

	io::Writer& operator<<(io::Writer& w, ItemData const& object)
	{
		w.WritePOD(object);
		return w;
	}

	io::Reader& operator>>(io::Reader& r, ItemData& object)
	{
		r.readPOD(object);
		return r;
	}

	Inventory::Inventory(GamePlayerS& owner)
		: m_owner(owner)
		, m_freeSlots(player_inventory_pack_slots::End - player_inventory_pack_slots::Start)	// Default slot count with only a backpack
		, m_nextBuyBackSlot(player_buy_back_slots::Start)
	{
	}

	InventoryChangeFailure Inventory::createItems(const proto::ItemEntry& entry, uint16 amount /* = 1*/, std::map<uint16, uint16>* out_addedBySlot /* = nullptr*/)
	{
		// Incorrect value used, so give at least one item
		if (amount == 0) {
			amount = 1;
		}

		// Limit the total amount of items
		const uint16 itemCount = getItemCount(entry.id());
		if (entry.maxcount() > 0 &&
			static_cast<uint32>(itemCount + amount) > entry.maxcount())
		{
			return inventory_change_failure::CantCarryMoreOfThis;
		}

		// Quick check if there are enough free slots (only works if we don't have an item of this type yet)
		const uint16 requiredSlots = (amount - 1) / entry.maxstack() + 1;
		if ((itemCount == 0 || entry.maxstack() <= 1) &&
			requiredSlots > m_freeSlots)
		{
			return inventory_change_failure::InventoryFull;
		}

		// We need to remember free slots, since we first want to stack items up as best as possible
		LinearSet<uint16> emptySlots;
		// We also need to remember all valid slots, that contain an item of that entry but are not
		// at the stack limit so we can fill up those stacks.
		LinearSet<uint16> usedCapableSlots;
		// This counter represent the number of available space for this item in total
		uint16 availableStacks = 0;

		// This variable is used so that we can take a shortcut. Since we know the total amount of
		// this item entry in the inventory, we can determine whether we have found all items
		uint16 itemsProcessed = 0;

		forEachBag([this, &itemsProcessed, &availableStacks, &usedCapableSlots, &emptySlots, &requiredSlots, &itemCount, &entry](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
			{
				for (uint8 slot = slotStart; slot < slotEnd; ++slot)
				{
					const uint16 absoluteSlot = getAbsoluteSlot(bag, slot);

					// Check if this slot is empty
					auto it = m_itemsBySlot.find(absoluteSlot);
					if (it == m_itemsBySlot.end())
					{
						// Increase counter
						availableStacks += entry.maxstack();

						// Remember this slot for later and skip it for now
						emptySlots.add(absoluteSlot);
						if (itemsProcessed >= itemCount &&
							emptySlots.size() >= requiredSlots)
						{
							break;
						}

						// If we processed all items, we want to make sure, that we found enough free slots as well
						continue;
					}

					// It is not empty, so check if the item is of the same entry
					if (it->second->GetEntry().id() != entry.id())
					{
						// Different item
						continue;
					}

					// Get the items stack count
					const uint32 stackCount = it->second->GetStackCount();
					itemsProcessed += stackCount;

					// Check if the item's stack limit is reached
					if (stackCount >= entry.maxstack())
					{
						if (itemsProcessed >= itemCount &&
							emptySlots.size() >= requiredSlots)
						{
							break;
						}

						// If we processed all items, we want to make sure, that we found enough free slots as well
						continue;
					}

					// Stack limit not reached, remember this slot
					availableStacks += (entry.maxstack() - stackCount);
					usedCapableSlots.add(absoluteSlot);
				}

				// We can stop now
				if (itemsProcessed >= itemCount &&
					emptySlots.size() >= requiredSlots)
				{
					return false;
				}

				return true;
			});

		// Now we can determine if there is enough space
		if (amount > availableStacks)
		{
			// Not enough space
			return inventory_change_failure::InventoryFull;
		}

		// Now finally create the items. First, fill up all used stacks
		uint16 amountLeft = amount;
		for (auto& slot : usedCapableSlots)
		{
			auto item = m_itemsBySlot[slot];

			// Added can not be greater than amountLeft, so we don't need a check on subtraction
			uint16 added = item->AddStacks(amountLeft);
			amountLeft -= added;

			// Notify update
			if (added > 0)
			{
				// Increase cached counter
				m_itemCounter[entry.id()] += added;

				if (out_addedBySlot) {
					out_addedBySlot->insert(std::make_pair(slot, added));
				}

				// Notify update
				itemInstanceUpdated(item, slot);
				if (isInventorySlot(slot) || isEquipmentSlot(slot) || isBagPackSlot(slot))
				{
					//m_owner.forceFieldUpdate(character_fields::InvSlotHead + ((slot & 0xFF) * 2));
					//m_owner.forceFieldUpdate(character_fields::InvSlotHead + ((slot & 0xFF) * 2) + 1);
				}
				else if (isBagSlot(slot))
				{
					auto bag = getBagAtSlot(slot);
					if (bag)
					{
						//bag->forceFieldUpdate(object_fields::Slot_1 + ((slot & 0xFF) * 2));
						//bag->forceFieldUpdate(object_fields::Slot_1 + ((slot & 0xFF) * 2) + 1);
						itemInstanceUpdated(bag, slot);
					}
				}
			}

			// Everything added
			if (amountLeft == 0) {
				break;
			}
		}

		// Are there still items left?
		if (amountLeft)
		{
			// Now iterate through all empty slots
			for (auto& slot : emptySlots)
			{
				// Create a new item instance (or maybe a bag if it is a bag)
				std::shared_ptr<GameItemS> item;
				if (entry.itemclass() == item_class::Container ||
					entry.itemclass() == item_class::Quiver)
				{
					item = std::make_shared<GameBagS>(m_owner.getProject(), entry);
				}
				else
				{
					item = std::make_shared<GameItemS>(m_owner.getProject(), entry);
				}
				item->Initialize();

				// We need a valid world instance for this
				auto* world = m_owner.GetWorldInstance();
				ASSERT(world);

				// Determine slot
				uint8 bag = 0, subslot = 0;
				getRelativeSlots(slot, bag, subslot);

				// Generate a new id for this item based on the characters world instance
				auto newItemId = world->getItemIdGenerator().generateId();
				item->Set<uint64>(object_fields::Guid, CreateEntryGUID(newItemId, entry.id(), GuidType::Item));
				if (isBagSlot(slot))
				{
					auto bagInst = getBagAtSlot(slot);
					item->Set<uint64>(object_fields::Contained, bagInst ? bagInst->GetGuid() : m_owner.GetGuid());
				}
				else
				{
					item->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
				}
				item->Set<uint64>(object_fields::ItemOwner, m_owner.GetGuid());

				// Bind this item
				if (entry.bonding() == item_binding::BindWhenPickedUp)
				{
					item->AddFlag(object_fields::ItemFlags, item_flags::Bound);
				}

				// One stack has been created by initializing the item
				amountLeft--;

				// Modify stack count
				const uint16 added = item->AddStacks(amountLeft);
				amountLeft -= added;

				// Increase cached counter
				m_itemCounter[entry.id()] += added + 1;
				if (out_addedBySlot) {
					out_addedBySlot->insert(std::make_pair(slot, added + 1));
				}

				// Add this item to the inventory slot and reduce our free slot cache
				m_itemsBySlot[slot] = item;
				m_freeSlots--;

				// Watch for item despawn packet
				m_itemDespawnSignals[item->GetGuid()]
					= item->despawned.connect(std::bind(&Inventory::onItemDespawned, this, std::placeholders::_1));

				// Create the item instance
				itemInstanceCreated(item, slot);

				// Update player fields
				if (bag == player_inventory_slots::Bag_0)
				{
					//m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());
					if (isBagBarSlot(slot))
					{
						//m_owner.ApplyItemStats(*item, true);
					}
					
					if (isEquipmentSlot(slot))
					{
						//m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (subslot * 16), item->GetEntry().id());
						//m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (subslot * 16), item->Get<uint64>(object_fields::Creator));
						//m_owner.ApplyItemStats(*item, true);
					}
				}
				else if (isBagSlot(slot))
				{
					auto packSlot = getAbsoluteSlot(player_inventory_slots::Bag_0, bag);
					auto bagInst = getBagAtSlot(packSlot);
					if (bagInst)
					{
						bagInst->Set<uint64>(object_fields::Slot_1 + (subslot * 2), item->GetGuid());
						itemInstanceUpdated(bagInst, packSlot);
					}
				}

				// All done
				if (amountLeft == 0) {
					break;
				}
			}
		}

		// WARNING: There should never be any items left here!
		ASSERT(amountLeft == 0);
		if (amountLeft > 0)
		{
			ELOG("Could not add all items, something went really wrong! " << __FUNCTION__);
			return inventory_change_failure::InventoryFull;
		}

		// Quest check
		//m_owner.onQuestItemAddedCredit(entry, amount);

		// Everything okay
		return inventory_change_failure::Okay;
	}

	InventoryChangeFailure Inventory::addItem(std::shared_ptr<GameItemS> item, uint16* out_slot)
	{
		ASSERT(item);

		const auto& entry = item->GetEntry();

		// Limit the total amount of items
		const uint16 itemCount = getItemCount(entry.id());
		if (entry.maxcount() > 0 &&
			static_cast<uint32>(itemCount + item->GetStackCount()) > entry.maxcount())
		{
			return inventory_change_failure::CantCarryMoreOfThis;
		}

		if (m_freeSlots < 1)
		{
			return inventory_change_failure::InventoryFull;
		}

		// Now check all bags for the next free slot
		uint16 targetSlot = 0;
		forEachBag([this, &targetSlot](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
			{
				for (uint8 slot = slotStart; slot < slotEnd; ++slot)
				{
					const uint16 absoluteSlot = getAbsoluteSlot(bag, slot);

					// Check if this slot is empty
					auto it = m_itemsBySlot.find(absoluteSlot);
					if (it == m_itemsBySlot.end())
					{
						// Slot is empty - use it!
						targetSlot = absoluteSlot;
						return false;
					}
				}

				// Continue with the next bag
				return true;
			});

		// Check if an empty slot was found
		if (targetSlot == 0)
		{
			return inventory_change_failure::InventoryFull;
		}

		// Finally add the item
		uint8 bag = 0, subslot = 0;
		getRelativeSlots(targetSlot, bag, subslot);

		// Fix item properties like container guid and owner guid
		if (isBagSlot(targetSlot))
		{
			auto bagInst = getBagAtSlot(targetSlot);
			ASSERT(bagInst);
			item->Set<uint64>(object_fields::Contained, bagInst ? bagInst->GetGuid() : m_owner.GetGuid());
		}
		else
		{
			item->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
		}
		item->Set<uint64>(object_fields::ItemOwner, m_owner.GetGuid());

		// Bind this item
		if (entry.bonding() == item_binding::BindWhenPickedUp)
		{
			item->AddFlag(object_fields::ItemFlags, item_flags::Bound);
		}

		// Increase cached counter
		m_itemCounter[entry.id()] += item->GetStackCount();
		if (out_slot) {
			*out_slot = targetSlot;
		}

		// Add this item to the inventory slot and reduce our free slot cache
		m_itemsBySlot[targetSlot] = item;
		m_freeSlots--;

		// Watch for item despawn packet
		m_itemDespawnSignals[item->GetGuid()]
			= item->despawned.connect(std::bind(&Inventory::onItemDespawned, this, std::placeholders::_1));

		// Create the item instance
		itemInstanceCreated(item, targetSlot);

		// Update player fields
		if (bag == player_inventory_slots::Bag_0)
		{
			//m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());
			if (isBagBarSlot(targetSlot))
			{
				//m_owner.ApplyItemStats(*item, true);
			}
			
			if (isEquipmentSlot(targetSlot))
			{
				//m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (subslot * 16), item->GetEntry().id());
				//m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (subslot * 16), item->Get<uint64>(object_fields::Creator));
				//m_owner.ApplyItemStats(*item, true);
			}
		}
		else if (isBagSlot(targetSlot))
		{
			auto packSlot = getAbsoluteSlot(player_inventory_slots::Bag_0, bag);
			auto bagInst = getBagAtSlot(packSlot);
			if (bagInst)
			{
				bagInst->Set<uint64>(object_fields::Slot_1 + (subslot * 2), item->GetGuid());
				itemInstanceUpdated(bagInst, packSlot);
			}
		}

		// Everything okay
		return inventory_change_failure::Okay;
	}

	InventoryChangeFailure Inventory::removeItems(const proto::ItemEntry& entry, uint16 amount)
	{
		// If amount equals 0, remove ALL items of that entry.
		const uint16 itemCount = getItemCount(entry.id());
		if (amount == 0)
		{
			amount = itemCount;
		}

		// We don't have enough items, so we don't need to bother iterating through
		if (itemCount < amount)
		{
			// Maybe use a different result
			return inventory_change_failure::ItemNotFound;
		}

		// Counter used to know when to stop iteration
		uint16 itemsToDelete = amount;

		forEachBag([this, &itemCount, &entry, &itemsToDelete](uint8 bag, uint8 slotStart, uint8 slotEnd) -> bool
			{
				for (uint8 slot = slotStart; slot < slotEnd; ++slot)
				{
					const uint16 absoluteSlot = getAbsoluteSlot(bag, slot);

					// Check if this slot is empty
					auto it = m_itemsBySlot.find(absoluteSlot);
					if (it == m_itemsBySlot.end())
					{
						// Empty slot
						continue;
					}

					// It is not empty, so check if the item is of the same entry
					if (it->second->GetEntry().id() != entry.id())
					{
						// Different item
						continue;
					}

					// Get the items stack count
					const uint32 stackCount = it->second->GetStackCount();
					if (stackCount <= itemsToDelete)
					{
						// Remove item at this slot
						auto result = removeItem(absoluteSlot);
						if (result != inventory_change_failure::Okay)
						{
							ELOG("Could not remove item at slot " << absoluteSlot);
						}
						else
						{
							// Reduce counter
							itemsToDelete -= stackCount;
						}
					}
					else
					{
						// Reduce stack count
						it->second->Set<uint32>(object_fields::StackCount, stackCount - itemsToDelete);
						m_itemCounter[entry.id()] -= itemsToDelete;
						itemsToDelete = 0;

						// Notify client about this update
						itemInstanceUpdated(it->second, slot);
					}

					// All items processed, we can stop here
					if (itemsToDelete == 0) {
						return false;
					}
				}

				return true;
			});

		// WARNING: There should never be any items left here!
		ASSERT(itemsToDelete == 0);
		ASSERT(m_itemCounter[entry.id()] == itemCount - amount);

		return inventory_change_failure::Okay;
	}

	InventoryChangeFailure Inventory::removeItem(uint16 absoluteSlot, uint16 stacks/* = 0*/, bool sold/* = false*/)
	{
		// Try to find item
		auto it = m_itemsBySlot.find(absoluteSlot);
		if (it == m_itemsBySlot.end())
		{
			return inventory_change_failure::ItemNotFound;
		}

		// Updated cached item counter
		const uint32 stackCount = it->second->GetStackCount();
		if (stacks == 0 || stacks > stackCount) {
			stacks = stackCount;
		}
		m_itemCounter[it->second->GetEntry().id()] -= stacks;

		// Increase ref count since the item instance might be destroyed below
		auto item = it->second;
		if (stackCount == stacks)
		{
			// No longer watch for item despawn
			m_itemDespawnSignals.erase(item->GetGuid());

			// Remove item from slot
			m_itemsBySlot.erase(it);
			m_freeSlots++;

			uint8 bag = 0, subslot = 0;
			getRelativeSlots(absoluteSlot, bag, subslot);
			if (bag == player_inventory_slots::Bag_0)
			{
				//m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), 0);
				if (isBagBarSlot(absoluteSlot))
				{
					//m_owner.ApplyItemStats(*item, false);
				}

				if (isEquipmentSlot(absoluteSlot))
				{
					//m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (subslot * 16), item->GetEntry().id());
					//m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (subslot * 16), item->Get<uint64>(object_fields::Creator));
					//m_owner.ApplyItemStats(*item, false);
				}
			}
			else if (isBagSlot(absoluteSlot))
			{
				auto packSlot = getAbsoluteSlot(player_inventory_slots::Bag_0, bag);
				auto bagInst = getBagAtSlot(packSlot);
				if (bagInst)
				{
					bagInst->Set<uint64>(object_fields::Slot_1 + (subslot * 2), 0);
					itemInstanceUpdated(bagInst, packSlot);
				}
			}

			// Notify about destruction
			if (!sold)
			{
				itemInstanceDestroyed(item, absoluteSlot);
			}
		}
		else
		{
			item->Set<uint32>(object_fields::StackCount, stackCount - stacks);
			itemInstanceUpdated(it->second, absoluteSlot);
		}

		// If the item has been sold...
		if (sold)
		{
			// Find the next free buyback slot
			uint16 oldestFieldSlot = 0;
			uint16 slot = player_buy_back_slots::Start;
			uint32 oldestSlotTime = std::numeric_limits<uint32>::max();

			do
			{
				const uint16 fieldSlot = slot - player_buy_back_slots::Start;
				const uint16 buyBackGuid = m_owner.Get<uint64>(object_fields::VendorBuybackSlot_1 + (fieldSlot * 2));
				if (buyBackGuid == 0)
				{
					// Found a free slot
					break;
				}
				else
				{
					const uint32 slotTime = m_owner.Get<uint32>(object_fields::BuybackTimestamp_1 + fieldSlot);
					if (slotTime < oldestSlotTime)
					{
						oldestSlotTime = slotTime;
						oldestFieldSlot = slot;
					}
				}
			} while (++slot < player_buy_back_slots::End);

			// Check if there is a slot available
			if (slot >= player_buy_back_slots::End)
			{
				// No free slot available, discard the oldest one
				auto itemInst = m_itemsBySlot[oldestFieldSlot];
				if (itemInst)
				{
					itemInstanceDestroyed(std::cref(itemInst), oldestFieldSlot);
					m_itemsBySlot.erase(oldestFieldSlot);
				}

				slot = oldestFieldSlot;
			}

			const uint32 slotTime = ::time(nullptr);
			const uint16 fieldSlot = slot - player_buy_back_slots::Start;

			// Save in new slot location
			m_itemsBySlot[slot] = item;

			// Update slots
			m_owner.Set<uint64>(object_fields::VendorBuybackSlot_1 + (fieldSlot * 2), item->GetGuid());
			m_owner.Set<uint32>(object_fields::BuybackPrice_1 + fieldSlot, item->GetEntry().sellprice() * stacks);
			m_owner.Set<uint32>(object_fields::BuybackTimestamp_1 + fieldSlot, slotTime + 30 * 3600);
		}

		// Quest check
		//m_owner.onQuestItemRemovedCredit(item->getEntry(), stacks);

		return inventory_change_failure::Okay;
	}

	InventoryChangeFailure Inventory::removeItemByGUID(uint64 guid, uint16 stacks)
	{
		uint16 slot = 0;
		if (!findItemByGUID(guid, slot))
		{
			return inventory_change_failure::InternalBagError;
		}

		return removeItem(slot, stacks);
	}

	InventoryChangeFailure Inventory::swapItems(uint16 slotA, uint16 slotB)
	{
		// We need a valid source slot
		auto srcItem = getItemAtSlot(slotA);
		auto dstItem = getItemAtSlot(slotB);
		if (!srcItem)
		{
			//m_owner.inventoryChangeFailure(game::inventory_change_failure::ItemNotFound, srcItem.get(), dstItem.get());
			return inventory_change_failure::ItemNotFound;
		}

		// Owner has to be alive
		if (!m_owner.IsAlive())
		{
			//m_owner.inventoryChangeFailure(game::inventory_change_failure::YouAreDead, srcItem.get(), dstItem.get());
			return inventory_change_failure::YouAreDead;
		}

		// Can't change equipment while in combat!
		if (m_owner.IsInCombat() && isEquipmentSlot(slotA))
		{
			if ((slotA & 0xFF) != player_equipment_slots::Mainhand &&
				(slotA & 0xFF) != player_equipment_slots::Offhand &&
				(slotA & 0xFF) != player_equipment_slots::Ranged)
			{
				//m_owner.inventoryChangeFailure(game::inventory_change_failure::NotInCombat, srcItem.get(), dstItem.get());
				return inventory_change_failure::NotInCombat;
			}
		}

		if (m_owner.IsInCombat() && isEquipmentSlot(slotB))
		{
			if ((slotB & 0xFF) != player_equipment_slots::Mainhand &&
				(slotB & 0xFF) != player_equipment_slots::Offhand &&
				(slotB & 0xFF) != player_equipment_slots::Ranged)
			{
				//m_owner.inventoryChangeFailure(game::inventory_change_failure::NotInCombat, srcItem.get(), dstItem.get());
				return inventory_change_failure::NotInCombat;
			}
		}

		// Verify destination slot for source item
		auto result = isValidSlot(slotB, srcItem->GetEntry());
		if (result != inventory_change_failure::Okay)
		{
			//m_owner.inventoryChangeFailure(result, srcItem.get(), dstItem.get());
			return result;
		}

		// If there is an item in the destination slot, also verify the source slot
		if (dstItem)
		{
			result = isValidSlot(slotA, dstItem->GetEntry());
			if (result != inventory_change_failure::Okay)
			{
				//m_owner.inventoryChangeFailure(result, srcItem.get(), dstItem.get());
				return result;
			}

			// Check if both items share the same entry
			if (srcItem->GetEntry().id() == dstItem->GetEntry().id())
			{
				const uint32 maxStack = srcItem->GetEntry().maxstack();
				if (maxStack > 1 && maxStack > dstItem->GetStackCount())
				{
					// Check if we can just add stacks to the destination item
					const uint32 availableDstStacks = maxStack - dstItem->GetStackCount();
					if (availableDstStacks > 0)
					{
						if (availableDstStacks < srcItem->GetStackCount())
						{
							// Everything swapped
							dstItem->AddStacks(availableDstStacks);
							srcItem->Set<uint32>(object_fields::StackCount, srcItem->GetStackCount() - availableDstStacks);
							itemInstanceUpdated(dstItem, slotB);
							itemInstanceUpdated(srcItem, slotA);
							return inventory_change_failure::Okay;
						}
						else	// Available Stacks >= srcItem stack count
						{
							dstItem->AddStacks(availableDstStacks);
							itemInstanceUpdated(dstItem, slotB);

							// Remove source item
							//m_owner.Set<uint64>(object_fields::InvSlotHead + (slotA & 0xFF) * 2, 0);

							// No longer watch for item despawn
							m_itemDespawnSignals.erase(srcItem->GetGuid());
							itemInstanceDestroyed(srcItem, slotA);
							m_itemsBySlot.erase(slotA);
							m_freeSlots++;

							return inventory_change_failure::Okay;
						}
					}
				}
			}
		}

		if (srcItem->GetTypeId() == ObjectTypeId::Container && !isBagPackSlot(slotB))
		{
			if (!std::dynamic_pointer_cast<GameBagS>(srcItem)->IsEmpty())
			{
				//m_owner.inventoryChangeFailure(game::inventory_change_failure::CanOnlyDoWithEmptyBags, srcItem.get(), dstItem.get());
				return inventory_change_failure::CanOnlyDoWithEmptyBags;
			}
		}

		// Everything seems to be okay, swap items
		if (isEquipmentSlot(slotA) || isInventorySlot(slotA) || isBagPackSlot(slotA))
		{
			//m_owner.Set<uint64>(object_fields::InvSlotHead + (slotA & 0xFF) * 2, (dstItem ? dstItem->GetGuid() : 0));

			if (dstItem &&
				dstItem->Get<uint64>(object_fields::Contained) != m_owner.GetGuid())
			{
				dstItem->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
				itemInstanceUpdated(dstItem, slotA);
			}
		}
		else if (isBagSlot(slotA))
		{
			auto bag = getBagAtSlot(slotA);
			if (!bag)
			{
				//m_owner.inventoryChangeFailure(game::inventory_change_failure::InternalBagError, srcItem.get(), dstItem.get());
				return inventory_change_failure::InternalBagError;
			}

			bag->Set<uint64>(object_fields::Slot_1 + (slotA & 0xFF) * 2, (dstItem ? dstItem->GetGuid() : 0));
			itemInstanceUpdated(bag, getAbsoluteSlot(player_inventory_slots::Bag_0, slotA >> 8));

			if (dstItem &&
				dstItem->Get<uint64>(object_fields::Contained) != bag->GetGuid())
			{
				dstItem->Set<uint64>(object_fields::Contained, bag->GetGuid());
				itemInstanceUpdated(dstItem, slotA);
			}
		}

		if (isEquipmentSlot(slotB) || isInventorySlot(slotB) || isBagPackSlot(slotB))
		{
			//m_owner.Set<uint64>(object_fields::InvSlotHead + (slotB & 0xFF) * 2, srcItem->GetGuid());

			if (srcItem->Get<uint64>(object_fields::Contained) != m_owner.GetGuid())
			{
				srcItem->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
				itemInstanceUpdated(srcItem, slotB);
			}
		}
		else if (isBagSlot(slotB))
		{
			auto bag = getBagAtSlot(slotB);
			if (!bag)
			{
				//m_owner.inventoryChangeFailure(inventory_change_failure::InternalBagError, srcItem.get(), dstItem.get());
				return inventory_change_failure::InternalBagError;
			}

			bag->Set<uint64>(object_fields::Slot_1 + (slotB & 0xFF) * 2, srcItem->GetGuid());
			itemInstanceUpdated(bag, getAbsoluteSlot(player_inventory_slots::Bag_0, slotA >> 8));

			if (srcItem->Get<uint64>(object_fields::Contained) != bag->GetGuid())
			{
				srcItem->Set<uint64>(object_fields::Contained, bag->GetGuid());
				itemInstanceUpdated(srcItem, slotB);
			}
		}

		// Adjust bag slots
		const bool isBagPackA = isBagPackSlot(slotA);
		const bool isBagBackB = isBagPackSlot(slotB);
		if (isBagPackA && !isBagBackB)
		{
			if (!dstItem &&
				srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots -= srcItem->Get<uint32>(object_fields::NumSlots);
			}
			else if (
				dstItem &&
				dstItem->GetTypeId() == ObjectTypeId::Container &&
				srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots -= srcItem->Get<uint32>(object_fields::NumSlots);
				m_freeSlots += dstItem->Get<uint32>(object_fields::NumSlots);
			}
		}
		else if (isBagBackB && !isBagPackA)
		{
			if (!dstItem &&
				srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots += srcItem->Get<uint32>(object_fields::NumSlots);
			}
			else if (
				dstItem &&
				dstItem->GetTypeId() == ObjectTypeId::Container &&
				srcItem->GetTypeId() == ObjectTypeId::Container)
			{
				m_freeSlots -= dstItem->Get<uint32>(object_fields::NumSlots);
				m_freeSlots += srcItem->Get<uint32>(object_fields::NumSlots);
			}
		}

		std::swap(m_itemsBySlot[slotA], m_itemsBySlot[slotB]);
		if (!dstItem)
		{
			// Remove new item in SlotA
			m_itemsBySlot.erase(slotA);

			// No item in slot B, and slot A was an inventory slot, so this gives us another free slot
			// if slot B is not an inventory/bag slot, too
			if ((isInventorySlot(slotA) || isBagSlot(slotA)) &&
				!(isInventorySlot(slotB) || isBagSlot(slotB)))
			{
				m_freeSlots++;
			}
			// No item in slot A, and slot B is an inventory slot, so this will use another free slot
			else if ((isInventorySlot(slotB) || isBagSlot(slotB)) &&
				!(isInventorySlot(slotA) || isBagSlot(slotA)))
			{
				ASSERT(m_freeSlots >= 1);
				m_freeSlots--;
			}
		}

		// Apply bag stats (mainly for quivers so far...)
		if (isBagBarSlot(slotA))
		{
			//m_owner.ApplyItemStats(*srcItem, false);
			if (dstItem)
			{
				//m_owner.ApplyItemStats(*dstItem, true);
			}
		}
		if (isBagBarSlot(slotB))
		{
			//m_owner.ApplyItemStats(*srcItem, true);
			if (dstItem)
			{
				//m_owner.ApplyItemStats(*dstItem, false);
			}
		}

		// Update visuals
		if (isEquipmentSlot(slotA))
		{
			//m_owner.Set<uint32>(object_fields::VisibleItem1_0 + ((slotA & 0xFF) * 16), (dstItem ? dstItem->GetEntry().id() : 0));
			//m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + ((slotA & 0xFF) * 16), (dstItem ? dstItem->Get<uint64>(object_fields::Creator) : 0));
			//m_owner.ApplyItemStats(*srcItem, false);
			if (srcItem->GetEntry().itemset() != 0)
			{
				onSetItemUnequipped(srcItem->GetEntry().itemset());
			}
			if (dstItem)
			{
				//m_owner.ApplyItemStats(*dstItem, true);
				if (dstItem->GetEntry().itemset() != 0)
				{
					onSetItemEquipped(dstItem->GetEntry().itemset());
				}
			}
		}
		if (isEquipmentSlot(slotB))
		{
			//m_owner.Set<uint32>(object_fields::VisibleItem1_0 + ((slotB & 0xFF) * 16), srcItem->GetEntry().id());
			//m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + ((slotB & 0xFF) * 16), srcItem->Get<uint64>(object_fields::Creator));
			//m_owner.ApplyItemStats(*srcItem, true);
			if (srcItem->GetEntry().itemset() != 0)
			{
				onSetItemEquipped(srcItem->GetEntry().itemset());
			}
			if (dstItem)
			{
				//m_owner.ApplyItemStats(*dstItem, false);
				if (dstItem->GetEntry().itemset() != 0)
				{
					onSetItemUnequipped(dstItem->GetEntry().itemset());
				}
			}

			// Bind this item
			if (srcItem->GetEntry().bonding() == item_binding::BindWhenEquipped)
			{
				srcItem->AddFlag(object_fields::ItemFlags, item_flags::Bound);
				itemInstanceUpdated(srcItem, slotB);
			}
		}
		else if (isBagPackSlot(slotB))
		{
			if (srcItem->GetEntry().bonding() == item_binding::BindWhenEquipped)
			{
				srcItem->AddFlag(object_fields::ItemFlags, item_flags::Bound);
			}
		}

		return inventory_change_failure::Okay;
	}

	namespace
	{
		weapon_prof::Type weaponProficiency(uint32 subclass)
		{
			switch (subclass)
			{
			case item_subclass_weapon::OneHandedAxe:
				return weapon_prof::OneHandAxe;
			case item_subclass_weapon::TwoHandedAxe:
				return weapon_prof::TwoHandAxe;
			case item_subclass_weapon::Bow:
				return weapon_prof::Bow;
			case item_subclass_weapon::CrossBow:
				return weapon_prof::Crossbow;
			case item_subclass_weapon::Dagger:
				return weapon_prof::Dagger;
			case item_subclass_weapon::Fist:
				return weapon_prof::Fist;
			case item_subclass_weapon::Gun:
				return weapon_prof::Gun;
			case item_subclass_weapon::OneHandedMace:
				return weapon_prof::OneHandMace;
			case item_subclass_weapon::TwoHandedMace:
				return weapon_prof::TwoHandMace;
			case item_subclass_weapon::Polearm:
				return weapon_prof::Polearm;
			case item_subclass_weapon::Staff:
				return weapon_prof::Staff;
			case item_subclass_weapon::OneHandedSword:
				return weapon_prof::OneHandSword;
			case item_subclass_weapon::TwoHandedSword:
				return weapon_prof::TwoHandSword;
			case item_subclass_weapon::Thrown:
				return weapon_prof::Throw;
			case item_subclass_weapon::Wand:
				return weapon_prof::Wand;
			}

			return weapon_prof::None;
		}

		armor_prof::Type armorProficiency(uint32 subclass)
		{
			switch (subclass)
			{
			case item_subclass_armor::Misc:
				return armor_prof::Common;
			case item_subclass_armor::Buckler:
			case item_subclass_armor::Shield:
				return armor_prof::Shield;
			case item_subclass_armor::Cloth:
				return armor_prof::Cloth;
			case item_subclass_armor::Leather:
				return armor_prof::Leather;
			case item_subclass_armor::Mail:
				return armor_prof::Mail;
			case item_subclass_armor::Plate:
				return armor_prof::Plate;
			}

			return armor_prof::None;
		}
	}

	InventoryChangeFailure Inventory::isValidSlot(uint16 slot, const proto::ItemEntry& entry) const
	{
		// Split the absolute slot
		uint8 bag = 0, subslot = 0;
		if (!getRelativeSlots(slot, bag, subslot)) {
			return inventory_change_failure::InternalBagError;
		}

		// Check if it is a special bag....
		if (isEquipmentSlot(slot))
		{
			/*
			auto armorProf = m_owner.GetArmorProficiency();
			auto weaponProf = m_owner.GetWeaponProficiency();

			// Determine whether the provided inventory type can go into the slot
			if (entry.itemclass() == item_class::Weapon)
			{
				if ((weaponProf & weaponProficiency(entry.subclass())) == 0)
				{
					return inventory_change_failure::NoRequiredProficiency;
				}
			}
			else if (entry.itemclass() == item_class::Armor)
			{
				if ((armorProf & armorProficiency(entry.subclass())) == 0)
				{
					return inventory_change_failure::NoRequiredProficiency;
				}
			}
			*/

			/*
			if (entry.requiredlevel() > 0 &&
				entry.requiredlevel() > m_owner.GetLevel())
			{
				return inventory_change_failure::CantEquipLevel;
			}
			*/

			/*
			if (entry.requiredskill() != 0 &&
				!m_owner.HasSkill(entry.requiredskill()))
			{
				return inventory_change_failure::CantEquipSkill;
			}
			*/

			// Validate equipment slots
			auto srcInvType = entry.inventorytype();
			switch (subslot)
			{
			case player_equipment_slots::Head:
				if (srcInvType != inventory_type::Head) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Body:
				if (srcInvType != inventory_type::Body) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Chest:
				if (srcInvType != inventory_type::Chest &&
					srcInvType != inventory_type::Robe) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Feet:
				if (srcInvType != inventory_type::Feet) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Neck:
				if (srcInvType != inventory_type::Neck) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Ranged:
				if (srcInvType != inventory_type::Ranged &&
					srcInvType != inventory_type::Thrown &&
					srcInvType != inventory_type::RangedRight) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Finger1:
			case player_equipment_slots::Finger2:
				if (srcInvType != inventory_type::Finger) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Trinket1:
			case player_equipment_slots::Trinket2:
				if (srcInvType != inventory_type::Trinket) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Hands:
				if (srcInvType != inventory_type::Hands) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Legs:
				if (srcInvType != inventory_type::Legs) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Mainhand:
				if (srcInvType != inventory_type::MainHandWeapon &&
					srcInvType != inventory_type::TwoHandedWeapon &&
					srcInvType != inventory_type::Weapon)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				else if (srcInvType == inventory_type::TwoHandedWeapon)
				{
					auto offhand = getItemAtSlot(getAbsoluteSlot(player_inventory_slots::Bag_0, player_equipment_slots::Offhand));
					if (offhand)
					{
						// We need to be able to store the offhand weapon in the inventory
						auto result = canStoreItems(offhand->GetEntry());
						if (result != inventory_change_failure::Okay)
						{
							return result;
						}
					}
				}
				break;
			case player_equipment_slots::Offhand:
				if (srcInvType != inventory_type::OffHandWeapon &&
					srcInvType != inventory_type::Shield &&
					srcInvType != inventory_type::Weapon &&
					srcInvType != inventory_type::Holdable)
				{
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				else
				{
					if (srcInvType != inventory_type::Shield &&
						srcInvType != inventory_type::Holdable /*&&
						!m_owner.CanDualWield()*/)
					{
						return inventory_change_failure::CantDualWield;
					}

					auto item = getItemAtSlot(getAbsoluteSlot(player_inventory_slots::Bag_0, player_equipment_slots::Mainhand));
					if (item &&
						item->GetEntry().inventorytype() == inventory_type::TwoHandedWeapon)
					{
						return inventory_change_failure::CantEquipWithTwoHanded;
					}
				}
				break;
			case player_equipment_slots::Shoulders:
				if (srcInvType != inventory_type::Shoulders) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Tabard:
				if (srcInvType != inventory_type::Tabard) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Waist:
				if (srcInvType != inventory_type::Waist) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Wrists:
				if (srcInvType != inventory_type::Wrists) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			case player_equipment_slots::Back:
				if (srcInvType != inventory_type::Cloak) {
					return inventory_change_failure::ItemDoesNotGoToSlot;
				}
				break;
			default:
				return inventory_change_failure::ItemDoesNotGoToSlot;
			}

			return inventory_change_failure::Okay;
		}
		else if (isInventorySlot(slot))
		{
			// TODO: Inventory slot validation? However, isInventorySlot already
			// performs some checks
			return inventory_change_failure::Okay;
		}
		else if (isBagSlot(slot))
		{
			// Validate bag
			auto bag = getBagAtSlot(slot);
			if (!bag)
			{
				return inventory_change_failure::ItemDoesNotGoToSlot;
			}

			if (subslot >= bag->GetSlotCount())
			{
				return inventory_change_failure::ItemDoesNotGoToSlot;
			}

			if (bag->GetEntry().itemclass() == item_class::Quiver &&
				entry.inventorytype() != inventory_type::Ammo)
			{
				return inventory_change_failure::OnlyAmmoCanGoHere;
			}

			return inventory_change_failure::Okay;
		}
		else if (isBagPackSlot(slot))
		{
			if (entry.itemclass() != item_class::Container &&
				entry.itemclass() != item_class::Quiver)
			{
				return inventory_change_failure::NotABag;
			}

			// Make sure that we have only up to one quiver equipped at a time
			if (entry.itemclass() == item_class::Quiver)
			{
				if (hasEquippedQuiver())
					return inventory_change_failure::CanEquipOnlyOneQuiver;
			}

			auto bagItem = getItemAtSlot(slot);
			if (bagItem)
			{
				if (bagItem->GetTypeId() != ObjectTypeId::Container)
				{
					// Return code valid? ...
					return inventory_change_failure::NotABag;
				}

				auto castedBag = std::static_pointer_cast<GameBagS>(bagItem);
				ASSERT(castedBag);

				if (!castedBag->IsEmpty())
				{
					return inventory_change_failure::CanOnlyDoWithEmptyBags;
				}
			}

			return inventory_change_failure::Okay;
		}

		return inventory_change_failure::InternalBagError;
	}

	InventoryChangeFailure Inventory::canStoreItems(const proto::ItemEntry& entry, uint16 amount) const
	{
		// Incorrect value used, so give at least one item
		if (amount == 0) {
			amount = 1;
		}

		// Limit the total amount of items
		const uint16 itemCount = getItemCount(entry.id());
		if (entry.maxcount() > 0 &&
			uint32(itemCount + amount) > entry.maxcount())
		{
			return inventory_change_failure::CantCarryMoreOfThis;
		}

		// Quick check if there are enough free slots (only works if we don't have an item of this type yet)
		const uint16 requiredSlots = (amount - 1) / entry.maxstack() + 1;
		if ((itemCount == 0 || entry.maxstack() <= 1) &&
			requiredSlots > m_freeSlots)
		{
			return inventory_change_failure::InventoryFull;
		}

		// TODO

		return inventory_change_failure::Okay;
	}

	uint16 Inventory::getItemCount(const uint32 itemId) const
	{
		const auto it = m_itemCounter.find(itemId);
		return (it != m_itemCounter.end() ? it->second : 0);
	}

	bool Inventory::hasEquippedQuiver() const
	{
		for (uint8 slot = player_inventory_slots::Start; slot < player_inventory_slots::End; ++slot)
		{
			const auto testBag = getItemAtSlot(getAbsoluteSlot(player_inventory_slots::Bag_0, slot));
			if (testBag && testBag->GetEntry().itemclass() == item_class::Quiver)
			{
				return true;
			}
		}

		return false;
	}

	uint16 Inventory::getAbsoluteSlot(uint8 bag, uint8 slot)
	{
		return (bag << 8) | slot;
	}

	bool Inventory::getRelativeSlots(uint16 absoluteSlot, uint8& out_bag, uint8& out_slot)
	{
		out_bag = static_cast<uint8>(absoluteSlot >> 8);
		out_slot = static_cast<uint8>(absoluteSlot & 0xFF);
		return true;
	}

	std::shared_ptr<GameItemS> Inventory::getItemAtSlot(uint16 absoluteSlot) const
	{
		if (const auto it = m_itemsBySlot.find(absoluteSlot); it != m_itemsBySlot.end())
		{
			return it->second;
		}

		return nullptr;
	}

	std::shared_ptr<GameBagS> Inventory::getBagAtSlot(uint16 absolute_slot) const
	{
		if (!isBagPackSlot(absolute_slot))
		{
			// Convert bag slot to bag pack slot which is 0xFFXX where XX is the bag slot id
			absolute_slot = getAbsoluteSlot(player_inventory_slots::Bag_0, static_cast<uint8>(absolute_slot >> 8));
		}

		auto it = m_itemsBySlot.find(absolute_slot);
		if (it != m_itemsBySlot.end() && it->second->GetTypeId() == ObjectTypeId::Container)
		{
			return std::dynamic_pointer_cast<GameBagS>(it->second);
		}

		return nullptr;
	}

	std::shared_ptr<GameItemS> Inventory::getWeaponByAttackType(WeaponAttack attackType, bool nonbroken, bool useable) const
	{
		uint8 slot;

		switch (attackType)
		{
		case weapon_attack::BaseAttack:
			slot = player_equipment_slots::Mainhand;
			break;
		case weapon_attack::OffhandAttack:
			slot = player_equipment_slots::Offhand;
			break;
		case weapon_attack::RangedAttack:
			slot = player_equipment_slots::Ranged;
			break;
		}

		std::shared_ptr<GameItemS> item = getItemAtSlot(getAbsoluteSlot(player_inventory_slots::Bag_0, slot));

		if (!item || item->GetEntry().itemclass() != item_class::Weapon)
		{
			return nullptr;
		}

		if (nonbroken && item->IsBroken())
		{
			return nullptr;
		}

		/*
		if (useable && !m_owner.CanUseWeapon(attackType))
		{
			return nullptr;
		}
		*/

		return item;
	}

	bool Inventory::findItemByGUID(uint64 guid, uint16& out_slot) const
	{
		for (auto& item : m_itemsBySlot)
		{
			if (item.second->GetGuid() == guid)
			{
				out_slot = item.first;
				return true;
			}
		}

		return false;
	}

	bool Inventory::isEquipmentSlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) < player_equipment_slots::End
			);
	}

	bool Inventory::isBagPackSlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) >= player_inventory_slots::Start &&
			(absoluteSlot & 0xFF) < player_inventory_slots::End
			);
	}

	bool Inventory::isInventorySlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) >= player_inventory_pack_slots::Start &&
			(absoluteSlot & 0xFF) < player_inventory_pack_slots::End
			);
	}

	bool Inventory::isBagSlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 >= player_inventory_slots::Start &&
			absoluteSlot >> 8 < player_inventory_slots::End);
	}

	bool Inventory::isBagBarSlot(uint16 absoluteSlot)
	{
		return (
			absoluteSlot >> 8 == player_inventory_slots::Bag_0 &&
			(absoluteSlot & 0xFF) >= player_inventory_slots::Start &&
			(absoluteSlot & 0xFF) < player_inventory_slots::End
			);
	}

	bool Inventory::isBuyBackSlot(uint16 absoluteSlot)
	{
		return (absoluteSlot >= player_buy_back_slots::Start && absoluteSlot < player_buy_back_slots::End);
	}

	uint32 Inventory::repairAllItems()
	{
		uint32 totalCost = 0;

		// Repair everything equipped and in main bag
		for (uint8 slot = player_equipment_slots::Start; slot < player_inventory_pack_slots::End; ++slot)
		{
			totalCost += repairItem(getAbsoluteSlot(player_inventory_slots::Bag_0, slot));
		}

		// Also check equipped bag contents
		for (uint8 bagSlot = player_inventory_slots::Start; bagSlot < player_inventory_slots::End; ++bagSlot)
		{
			const uint16 absoluteBagSlot = getAbsoluteSlot(player_inventory_slots::Bag_0, bagSlot);

			// Check if bag exists
			auto bag = std::static_pointer_cast<GameBagS>(getItemAtSlot(absoluteBagSlot));
			if (!bag)
			{
				continue;
			}

			// Iterate bag items
			for (uint8 bagItemSlot = 0; bagItemSlot < static_cast<uint8>(bag->GetSlotCount()); ++bagItemSlot)
			{
				totalCost += repairItem(getAbsoluteSlot(bagSlot, bagItemSlot));
			}
		}

		return totalCost;
	}

	uint32 Inventory::repairItem(uint16 absoluteSlot)
	{
		uint32 totalCost = 0;

		// Look for item instance
		auto item = getItemAtSlot(absoluteSlot);
		if (!item)
		{
			return totalCost;
		}

		// Get max durability
		const uint32 maxDurability = item->GetEntry().durability();
		if (maxDurability == 0)
		{
			return totalCost;
		}

		// Get current durability
		const uint32 durability = item->getUInt32Value(object_fields::Durability);
		if (durability >= maxDurability)
		{
			return totalCost;
		}

		// Okay, we have something to repair
		// TODO: Calculate repair cost and try to consume money

		// Repair the item and notify the owner
		item->setUInt32Value(object_fields::Durability, maxDurability);
		itemInstanceUpdated(std::cref(item), 0);

		// Reapply item stats if needed
		if (durability == 0 && isEquipmentSlot(absoluteSlot))
		{
			m_owner.ApplyItemStats(*item, true);
		}

		return totalCost;
	}

	void Inventory::addRealmData(const ItemData& data)
	{
		m_realmData.push_back(data);
	}

	void Inventory::addSpawnBlocks(std::vector<std::vector<char>>& out_blocks)
	{
		// Reconstruct realm data if available
		if (!m_realmData.empty())
		{
			// World instance has to be ready
			auto* world = m_owner.GetWorldInstance();
			if (!world)
			{
				return;
			}

			// We need to store bag items for later, since we first need to create all bags
			std::map<uint16, std::shared_ptr<GameItemS>> bagItems;

			// Iterate through all entries
			for (auto& data : m_realmData)
			{
				auto* entry = m_owner.getProject().items.getById(data.entry);
				if (!entry)
				{
					ELOG("Could not find item " << data.entry);
					continue;
				}

				// Create a new item instance
				std::shared_ptr<GameItemS> item;
				if (entry->itemclass() == item_class::Container ||
					entry->itemclass() == item_class::Quiver)
				{
					item = std::make_shared<GameBagS>(m_owner.getProject(), *entry);
				}
				else
				{
					item = std::make_shared<GameItemS>(m_owner.getProject(), *entry);
				}
				
				auto newItemId = world->getItemIdGenerator().generateId();
				item->Set<int64>(object_fields::Guid, CreateEntryGUID(newItemId, entry->id(), GuidType::Item));
				item->Initialize();
				item->Set<uint64>(object_fields::ItemOwner, m_owner.GetGuid());
				//item->setUInt64Value(item_fields::Creator, data.creator);
				item->Set<uint64>(object_fields::Contained, m_owner.GetGuid());
				item->Set<uint32>(object_fields::Durability, data.durability);
				if (entry->bonding() == item_binding::BindWhenPickedUp)
				{
					item->AddFlag(object_fields::ItemFlags, item_flags::Bound);
				}

				// Add this item to the inventory slot
				m_itemsBySlot[data.slot] = item;

				// Determine slot
				uint8 bag = 0, subslot = 0;
				getRelativeSlots(data.slot, bag, subslot);
				if (bag == player_inventory_slots::Bag_0)
				{
					if (isBagBarSlot(data.slot))
					{
						//m_owner.ApplyItemStats(*item, true);
					}

					if (isEquipmentSlot(data.slot))
					{
						//m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());
						//m_owner.Set<uint32>(object_fields::VisibleItem1_0 + (subslot * 16), item->GetEntry().id());
						//m_owner.Set<uint64>(object_fields::VisibleItem1_CREATOR + (subslot * 16), item->Get<uint64>(object_fields::Creator));
						//m_owner.ApplyItemStats(*item, true);
						if (item->GetEntry().itemset() != 0)
						{
							onSetItemEquipped(item->GetEntry().itemset());
						}

						// Apply bonding
						if (entry->bonding() == item_binding::BindWhenEquipped)
						{
							item->AddFlag(object_fields::ItemFlags, item_flags::Bound);
						}
					}
					else if (isInventorySlot(data.slot))
					{
						//m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());
					}
					else if (isBagPackSlot(data.slot) && item->GetTypeId() == ObjectTypeId::Container)
					{
						//m_owner.Set<uint64>(object_fields::InvSlotHead + (subslot * 2), item->GetGuid());

						// Increase slot count since this is an equipped bag
						m_freeSlots += reinterpret_cast<GameBagS*>(item.get())->GetSlotCount();

						// Apply bonding
						if (entry->bonding() == item_binding::BindWhenEquipped)
						{
							item->AddFlag(object_fields::ItemFlags, item_flags::Bound);
						}
					}
				}
				else if (isBagSlot(data.slot))
				{
					bagItems[data.slot] = item;
				}

				// Modify stack count
				(void)item->AddStacks(data.stackCount - 1);
				m_itemCounter[data.entry] += data.stackCount;

				// Watch for item despawn packet
				m_itemDespawnSignals[item->GetGuid()]
					= item->despawned.connect(std::bind(&Inventory::onItemDespawned, this, std::placeholders::_1));

				// Quest check
				//m_owner.onQuestItemAddedCredit(item->GetEntry(), data.stackCount);

				// Inventory slot used
				if (isInventorySlot(data.slot) || isBagSlot(data.slot)) {
					m_freeSlots--;
				}
			}

			// Clear realm data since we don't need it any more
			m_realmData.clear();

			// Store items in bags
			for (auto& pair : bagItems)
			{
				auto bag = getBagAtSlot(getAbsoluteSlot(player_inventory_slots::Bag_0, pair.first >> 8));
				if (!bag)
				{
					ELOG("Could not find bag at slot " << pair.first << ": Maybe this bag is sent after the item");
				}
				else
				{
					pair.second->Set<uint64>(object_fields::Contained, bag->GetGuid());
					bag->Set<uint64>(object_fields::Slot_1 + ((pair.first & 0xFF) * 2), pair.second->GetGuid());
				}
			}
		}

		for (auto& pair : m_itemsBySlot)
		{
			std::vector<char> createItemBlock;
			io::VectorSink createItemSink(createItemBlock);
			io::Writer createItemWriter(createItemSink);
			{
				uint8 updateType = 0x02;						// Item
				uint8 updateFlags = 0x08 | 0x10;				//
				uint64 guid = pair.second->GetGuid();

				auto objectTypeId = pair.second->GetTypeId();	// Item

				// Header with object guid and type
				createItemWriter
					<< io::write<uint8>(updateType)
					<< io::write_packed_guid(guid)
					<< io::write<uint8>(objectTypeId)
					<< io::write<uint8>(updateFlags);
				if (updateFlags & 0x08)
				{
					createItemWriter
						<< io::write<uint32>(guidLowerPart(guid));
				}
				if (updateFlags & 0x10)
				{
					createItemWriter
						<< io::write<uint32>((guid << 48) & 0x0000FFFF);
				}

				//pair.second->WriteValueUpdateBlock(createItemWriter, m_owner, true);
			}
			out_blocks.push_back(std::move(createItemBlock));
		}
	}

	void Inventory::forEachBag(BagCallbackFunc callback)
	{
		// Enumerates all possible bags
		static std::array<uint8, 5> bags =
		{
			player_inventory_slots::Bag_0,
			player_inventory_slots::Start + 0,
			player_inventory_slots::Start + 1,
			player_inventory_slots::Start + 2,
			player_inventory_slots::Start + 3
		};

		for (const auto& bag : bags)
		{
			uint8 slotStart = 0, slotEnd = 0;
			if (bag == player_inventory_slots::Bag_0)
			{
				slotStart = player_inventory_pack_slots::Start;
				slotEnd = player_inventory_pack_slots::End;
			}
			else
			{
				auto bagInst = getBagAtSlot(getAbsoluteSlot(player_inventory_slots::Bag_0, bag));
				if (!bagInst)
				{
					// Skip this bag
					continue;
				}

				slotStart = 0;
				slotEnd = bagInst->GetSlotCount();
			}

			if (slotEnd <= slotStart)
			{
				continue;
			}

			if (!callback(bag, slotStart, slotEnd))
			{
				break;
			}
		}
	}

	void Inventory::onItemDespawned(GameObjectS& object)
	{
		if (object.GetTypeId() != ObjectTypeId::Item &&
			object.GetTypeId() != ObjectTypeId::Container)
		{
			return;
		}

		// Find item slot by guid
		uint16 slot = 0;
		if (!findItemByGUID(object.GetGuid(), slot))
		{
			WLOG("Could not find item by slot!");
			return;
		}

		// Destroy this item
		removeItem(slot);
	}

	void Inventory::onSetItemEquipped(uint32 set)
	{
		/*
		auto* setEntry = m_owner.getProject().itemSets.getById(set);
		if (!setEntry)
		{
			return;
		}

		SpellTargetMap targetMap;
		targetMap.SetTargetMap(spell_cast_target_flags::Unit);
		targetMap.SetUnitTarget(m_owner.GetGuid());

		auto& counter = m_setItems[set];
		for (auto& spell : setEntry->spells())
		{
			if (spell.itemcount() == counter + 1)
			{
				// Apply spell
				m_owner.CastSpell(targetMap, spell.spell(), 0);
			}
		}

		// Increment counter
		counter++;
		*/
	}

	void Inventory::onSetItemUnequipped(uint32 set)
	{
		/*
		auto* setEntry = m_owner.getProject().itemSets.getById(set);
		if (!setEntry)
		{
			return;
		}
		*/

		/*
		auto& counter = m_setItems[set];
		for (auto& spell : setEntry->spells())
		{
			if (spell.itemcount() == counter)
			{
				// Remove spell auras
				m_owner.GetAuras().removeAllAurasDueToSpell(spell.spell());
			}
		}

		// Increment counter
		counter--;
		*/
	}

	io::Writer& operator << (io::Writer& w, Inventory const& object)
	{
		if (object.m_realmData.empty())
		{
			// Inventory has actual item instances, so we serialize this object for realm usage
			w
				<< io::write<uint16>(object.m_itemsBySlot.size());
			for (const auto& pair : object.m_itemsBySlot)
			{
				ItemData data;
				data.entry = pair.second->GetEntry().id();
				data.slot = pair.first;
				data.stackCount = pair.second->GetStackCount();
				data.creator = pair.second->Get<uint64>(object_fields::Creator);
				data.contained = pair.second->Get<uint64>(object_fields::Contained);
				data.durability = pair.second->Get<uint32>(object_fields::Durability);
				data.randomPropertyIndex = 0;
				data.randomSuffixIndex = 0;
				w
					<< data;
			}
		}
		else
		{
			// Inventory has realm data left, and no item instances
			w
				<< io::write<uint16>(object.m_realmData.size());
			for (const auto& data : object.m_realmData)
			{
				w
					<< data;
			}
		}

		return w;
	}

	io::Reader& operator >> (io::Reader& r, Inventory& object)
	{
		object.m_itemsBySlot.clear();
		object.m_freeSlots = player_inventory_pack_slots::End - player_inventory_pack_slots::Start;
		object.m_itemCounter.clear();
		object.m_realmData.clear();

		// Read amount of items
		uint16 itemCount = 0;
		r >> io::read<uint16>(itemCount);

		// Read realm data
		object.m_realmData.resize(itemCount);
		for (uint16 i = 0; i < itemCount; ++i)
		{
			r >> object.m_realmData[i];
		}

		return r;
	}
}
