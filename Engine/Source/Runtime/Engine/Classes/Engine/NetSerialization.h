// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NetworkSerialization.h: 
	Contains custom network serialization functionality.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "Serialization/BitReader.h"
#include "Misc/NetworkGuid.h"
#include "UObject/CoreNet.h"
#include "EngineLogs.h"
#include "Containers/ArrayView.h"
#include "Net/GuidReferences.h"
#include "HAL/IConsoleManager.h"
#include "NetSerialization.generated.h"

class Error;
struct FFastArraySerializer;
struct FFastArraySerializerItem;

DECLARE_CYCLE_STAT_EXTERN(TEXT("NetSerializeFast Array"), STAT_NetSerializeFastArray, STATGROUP_ServerCPU, ENGINE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("NetSerializeFast Array BuildMap"), STAT_NetSerializeFastArray_BuildMap, STATGROUP_ServerCPU, ENGINE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("NetSerializeFast Array Delta Struct"), STAT_NetSerializeFastArray_DeltaStruct, STATGROUP_ServerCPU, ENGINE_API);

extern ENGINE_API TAutoConsoleVariable<int32> CVarNetEnableDetailedScopeCounters;

/**
 * Helper to optionally serialize a value (using operator<< on Archive).
 * A single signal bit is indicates whether to serialize, or whether to just use the default value.
 * Returns true if the value was not the default and needed to be serialized.
 */
template<typename ValueType>
bool SerializeOptionalValue(const bool bIsSaving, FArchive& Ar, ValueType& Value, const ValueType& DefaultValue)
{
	bool bNotDefault = (bIsSaving && (Value != DefaultValue));
	Ar.SerializeBits(&bNotDefault, 1);
	if (bNotDefault)
	{
		// Non-default value, need to save or load it.
		Ar << Value;
	}
	else if (!bIsSaving)
	{
		// Loading, and should use default
		Value = DefaultValue;
	}

	return bNotDefault;
}

/**
 * Helper to optionally serialize a value (using the NetSerialize function).
 * A single signal bit indicates whether to serialize, or whether to just use the default value.
 * Returns true if the value was not the default and needed to be serialized.
 */
template<typename ValueType>
bool NetSerializeOptionalValue(const bool bIsSaving, FArchive& Ar, ValueType& Value, const ValueType& DefaultValue, class UPackageMap* PackageMap)
{
	bool bNotDefault = (bIsSaving && (Value != DefaultValue));
	Ar.SerializeBits(&bNotDefault, 1);
	if (bNotDefault)
	{
		// Non-default value, need to save or load it.
		bool bLocalSuccess = true;
		Value.NetSerialize(Ar, PackageMap, bLocalSuccess);
	}
	else if (!bIsSaving)
	{
		// Loading, and should use default
		Value = DefaultValue;
	}

	return bNotDefault;
}

/**
 *	===================== NetSerialize and NetDeltaSerialize customization. =====================
 *
 *	The main purpose of this file it to hold custom methods for NetSerialization and NetDeltaSerialization. A longer explanation on how this all works is
 *	covered below. For quick reference however, this is how to customize net serialization for structs.
 *
 *		
 *	To define your own NetSerialize and NetDeltaSerialize on a structure:
 *		(of course you don't need to define both! Usually you only want to define one, but for brevity Im showing both at once)
 */
#if 0

USTRUCT()
struct FExampleStruct
{
	GENERATED_USTRUCT_BODY()
	
	/**
	 * @param Ar			FArchive to read or write from.
	 * @param Map			PackageMap used to resolve references to UObject*
	 * @param bOutSuccess	return value to signify if the serialization was succesfull (if false, an error will be logged by the calling function)
	 *
	 * @return return true if the serialization was fully mapped. If false, the property will be considered 'dirty' and will replicate again on the next update.
	 *	This is needed for UActor* properties. If an actor's Actorchannel is not fully mapped, properties referencing it must stay dirty.
	 *	Note that UPackageMap::SerializeObject returns false if an object is unmapped. Generally, you will want to return false from your ::NetSerialize
	 *  if you make any calls to ::SerializeObject that return false.
	 *
	*/
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		// Your code here!
		return true;
	}

	/**
	 * @param DeltaParms	Generic struct of input parameters for delta serialization
	 *
	 * @return return true if the serialization was fully mapped. If false, the property will be considered 'dirty' and will replicate again on the next update.
	 *	This is needed for UActor* properties. If an actor's Actorchannel is not fully mapped, properties referencing it must stay dirty.
	 *	Note that UPackageMap::SerializeObject returns false if an object is unmapped. Generally, you will want to return false from your ::NetSerialize
	 *  if you make any calls to ::SerializeObject that return false.
	 *
	*/
	bool NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms)
	{
		// Your code here!
		return true;
	}
}

template<>
struct TStructOpsTypeTraits< FExampleStruct > : public TStructOpsTypeTraitsBase2< FExampleStruct >
{
	enum 
	{
		WithNetSerializer = true,
		WithNetDeltaSerializer = true,
	};
};

#endif

/**
 *	===================== Fast TArray Replication ===================== 
 *
 *	Fast TArray Replication is a custom implementation of NetDeltaSerialize that is suitable for TArrays of UStructs. It offers performance
 *	improvements for large data sets, it serializes removals from anywhere in the array optimally, and allows events to be called on clients
 *	for adds and removals. The downside is that you will need to have game code mark items in the array as dirty, and well as the *order* of the list
 *	is not guaranteed to be identical between client and server in all cases.
 *
 *	Using FTR is more complicated, but this is the code you need:
 *
 */
#if 0
	
/** Step 1: Make your struct inherit from FFastArraySerializerItem */
USTRUCT()
struct FExampleItemEntry : public FFastArraySerializerItem
{
	GENERATED_USTRUCT_BODY()

	// Your data:
	UPROPERTY()
	int32		ExampleIntProperty;	

	UPROPERTY()
	float		ExampleFloatProperty;


	/** 
	 * Optional functions you can implement for client side notification of changes to items; 
	 * Parameter type can match the type passed as the 2nd template parameter in associated call to FastArrayDeltaSerialize
	 * 
	 * NOTE: It is not safe to modify the contents of the array serializer within these functions, nor to rely on the contents of the array 
	 * being entirely up-to-date as these functions are called on items individually as they are updated, and so may be called in the middle of a mass update.
	 */
	void PreReplicatedRemove(const struct FExampleArray& InArraySerializer);
	void PostReplicatedAdd(const struct FExampleArray& InArraySerializer);
	void PostReplicatedChange(const struct FExampleArray& InArraySerializer);

	// Optional: debug string used with LogNetFastTArray logging
	FString GetDebugString();

};

/** Step 2: You MUST wrap your TArray in another struct that inherits from FFastArraySerializer */
USTRUCT()
struct FExampleArray: public FFastArraySerializer
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FExampleItemEntry>	Items;	/** Step 3: You MUST have a TArray named Items of the struct you made in step 1. */

	/** Step 4: Copy this, replace example with your names */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms)
	{
	   return FFastArraySerializer::FastArrayDeltaSerialize<FExampleItemEntry, FExampleArray>( Items, DeltaParms, *this );
	}
};

/** Step 5: Copy and paste this struct trait, replacing FExampleArray with your Step 2 struct. */
template<>
struct TStructOpsTypeTraits< FExampleArray > : public TStructOpsTypeTraitsBase2< FExampleArray >
{
       enum 
       {
			WithNetDeltaSerializer = true,
       };
};

#endif

/** Step 6 and beyond: 
 *		-Declare a UPROPERTY of your FExampleArray (step 2) type.
 *		-You MUST call MarkItemDirty on the FExampleArray when you change an item in the array. You pass in a reference to the item you dirtied. 
 *			See FFastArraySerializer::MarkItemDirty.
 *		-You MUST call MarkArrayDirty on the FExampleArray if you remove something from the array.
 *		-In your classes GetLifetimeReplicatedProps, use DOREPLIFETIME(YourClass, YourArrayStructPropertyName);
 *
 *		You can override the following virtual functions in your structure (step 1) to get notifies before add/deletes/removes:
 *			-void PreReplicatedRemove(const FFastArraySerializer& Serializer)
 *			-void PostReplicatedAdd(const FFastArraySerializer& Serializer)
 *			-void PostReplicatedChange(const FFastArraySerializer& Serializer)
 *
 *		Thats it!
 */ 




/**
 *	
 *	===================== An Overview of Net Serialization and how this all works =====================
 *
 *		Everything originates in UNetDriver::ServerReplicateActors.
 *		Actors are chosen to replicate, create actor channels, and UActorChannel::ReplicateActor is called.
 *		ReplicateActor is ultimately responsible for deciding what properties have changed, and constructing an FOutBunch to send to clients.
 *
 *	The UActorChannel has 2 ways to decide what properties need to be sent.
 *		The traditional way, which is a flat TArray<uint8> buffer: UActorChannel::Recent. This represents a flat block of the actor properties.
 *		This block literally can be cast to an AActor* and property values can be looked up if you know the FProperty offset.
 *		The Recent buffer represents the values that the client using this actor channel has. We use recent to compare to current, and decide what to send.
 *
 *		This works great for 'atomic' properties; ints, floats, object*, etc.
 *		It does not work for 'dynamic' properties such as TArrays, which store values Num/Max but also a pointer to their array data,
 *		The array data has no where to fit in the flat ::Recent buffer. (Dynamic is probably a bad name for these properties)
 *
 *		To get around this, UActorChannel also has a TMap for 'dynamic' state. UActorChannel::RecentDynamicState. This map allows us to look up
 *		a 'base state' for a property given a property's RepIndex.
 *
 *	NetSerialize & NetDeltaSerialize
 *		Properties that fit into the flat Recent buffer can be serialized entirely with NetSerialize. NetSerialize just reads or writes to an FArchive.
 *		Since the replication can just look at the Recent[] buffer and do a direct comparison, it can tell what properties are dirty. NetSerialize just
 *		reads or writes.
 *
 *		Dynamic properties can only be serialized with NetDeltaSerialize. NetDeltaSerialize is serialization from a given base state, and produces
 *		both a 'delta' state (which gets sent to the client) and a 'full' state (which is saved to be used as the base state in future delta serializes).
 *		NetDeltaSerialize essentially does the diffing as well as the serialization. It must do the diffing so it can know what parts of the property it must
 *		send.
 *	
 *	Base States and dynamic properties replication.
 *		As far as the replication system / UActorChannel is concerned, a base state can be anything. The base state only deals with INetDeltaBaseState*.
 *
 *		UActorChannel::ReplicateActor will ultimately decide whether to call FProperty::NetSerializeItem or FProperty::NetDeltaSerializeItem.
 *
 *		As mentioned above NetDeltaSerialize takes in an extra base state and produces a diff state and a full state. The full state produced is used
 *		as the base state for future delta serialization. NetDeltaSerialize uses the base state and the current values of the actor to determine what parts
 *		it needs to send.
 *		
 *		The INetDeltaBaseStates are created within the NetDeltaSerialize functions. The replication system / UActorChannel does not know about the details.
 *
 *		Right now, there are 2 forms of delta serialization: Generic Replication and Fast Array Replication.
 *
 *	
 *	Generic Delta Replication
 *		Generic Delta Replication is implemented by FStructProperty::NetDeltaSerializeItem, FArrayProperty::NetDeltaSerializeItem, FProperty::NetDeltaSerializeItem.
 *		It works by first NetSerializing the current state of the object (the 'full' state) and using memcmp to compare it to previous base state. FProperty
 *		is what actually implements the comparison, writing the current state to the diff state if it has changed, and always writing to the full state otherwise.
 *		The FStructProperty and FArrayProperty functions work by iterating their fields or array elements and calling the FProperty function, while also embedding
 *		meta data. 
 *
 *		For example FArrayProperty basically writes: 
 *			"Array has X elements now" -> "Here is element Y" -> Output from FProperty::NetDeltaSerialize -> "Here is element Z" -> etc
 *
 *		Generic Data Replication is the 'default' way of handling FArrayProperty and FStructProperty serialization. This will work for any array or struct with any 
 *		sub properties as long as those properties can NetSerialize.
 *
 *	Custom Net Delta Serialiation
 *		Custom Net Delta Serialiation works by using the struct trait system. If a struct has the WithNetDeltaSerializer trait, then its native NetDeltaSerialize
 *		function will be called instead of going through the Generic Delta Replication code path in FStructProperty::NetDeltaSerializeItem.
 *
 *	Fast TArray Replication
 *		Fast TArray Replication is implemented through custom net delta serialization. Instead of a flat TArray buffer to repesent states, it only is concerned
 *		with a TMap of IDs and ReplicationKeys. The IDs map to items in the array, which all have a ReplicationID field defined in FFastArraySerializerItem.
 *		FFastArraySerializerItem also has a ReplicationKey field. When items are marked dirty with MarkItemDirty, they are given a new ReplicationKey, and assigned
 *		a new ReplicationID if they don't have one.
 *
 *		FastArrayDeltaSerialize (defined below)
 *		During server serialization (writing), we compare the old base state (e.g, the old ID<->Key map) with the current state of the array. If items are missing
 *		we write them out as deletes in the bunch. If they are new or changed, they are written out as changed along with their state, serialized via a NetSerialize call.
 *
 *		For example, what actually is written may look like:
 *			"Array has X changed elements, Y deleted elements" -> "element A changed" -> Output from NetSerialize on rest of the struct item -> "Element B was deleted" -> etc
 *
 *		Note that the ReplicationID is replicated and in sync between client and server. The indices are not.
 *
 *		During client serialization (reading), the client reads in the number of changed and number of deleted elements. It also builds a mapping of ReplicationID -> local index of the current array.
 *		As it deserializes IDs, it looks up the element and then does what it needs to (create if necessary, serialize in the current state, or delete).
 *
 *		There is currently no delta serialization done on the inner structures. If a ReplicationKey changes, the entire item is serialized. If we had
 *		use cases where we needed it, we could delta serialization on the inner dynamic properties. This could be done with more struct customization.
 *
 *		ReplicationID and ReplicationKeys are set by the MarkItemDirty function on FFastArraySerializer. These are just int32s that are assigned in order as things change.
 *		There is nothing special about them other than being unique.
 */

/** Custom INetDeltaBaseState used by Fast Array Serialization */
class FNetFastTArrayBaseState : public INetDeltaBaseState
{
public:

	FNetFastTArrayBaseState()
		: ArrayReplicationKey(INDEX_NONE)
	{}

	virtual bool IsStateEqual(INetDeltaBaseState* OtherState)
	{
		FNetFastTArrayBaseState * Other = static_cast<FNetFastTArrayBaseState*>(OtherState);
		for (auto It = IDToCLMap.CreateIterator(); It; ++It)
		{
			auto Ptr = Other->IDToCLMap.Find(It.Key());
			if (!Ptr || *Ptr != It.Value())
			{
				return false;
			}
		}
		return true;
	}

	/** Maps an element's Replication ID to Index. */
	TMap<int32, int32> IDToCLMap;

	int32 ArrayReplicationKey;
};


/** Base struct for items using Fast TArray Replication */
USTRUCT()
struct FFastArraySerializerItem
{
	GENERATED_USTRUCT_BODY()

	FFastArraySerializerItem()
		: ReplicationID(INDEX_NONE), ReplicationKey(INDEX_NONE), MostRecentArrayReplicationKey(INDEX_NONE)
	{

	}

	FFastArraySerializerItem(const FFastArraySerializerItem &InItem)
		: ReplicationID(INDEX_NONE), ReplicationKey(INDEX_NONE), MostRecentArrayReplicationKey(INDEX_NONE)
	{

	}

	FFastArraySerializerItem& operator=(const FFastArraySerializerItem& In)
	{
		if (&In != this)
		{
			ReplicationID = INDEX_NONE;
			ReplicationKey = INDEX_NONE;
			MostRecentArrayReplicationKey = INDEX_NONE;
		}
		return *this;
	}

	UPROPERTY(NotReplicated)
	int32 ReplicationID;

	UPROPERTY(NotReplicated)
	int32 ReplicationKey;

	UPROPERTY(NotReplicated)
	int32 MostRecentArrayReplicationKey;
	
	/**
	 * Called right before deleting element during replication.
	 * 
	 * @param InArraySerializer	Array serializer that owns the item and has triggered the replication call
	 * 
	 * NOTE: intentionally not virtual; invoked via templated code, @see FExampleItemEntry
	 */
	FORCEINLINE void PreReplicatedRemove(const struct FFastArraySerializer& InArraySerializer) { }
	/**
	 * Called after adding and serializing a new element
	 *
	 * @param InArraySerializer	Array serializer that owns the item and has triggered the replication call
	 * 
	 * NOTE: intentionally not virtual; invoked via templated code, @see FExampleItemEntry
	 */
	FORCEINLINE void PostReplicatedAdd(const struct FFastArraySerializer& InArraySerializer) { }
	/**
	 * Called after updating an existing element with new data
	 *
	 * @param InArraySerializer	Array serializer that owns the item and has triggered the replication call
	 * NOTE: intentionally not virtual; invoked via templated code, @see FExampleItemEntry
	 */
	FORCEINLINE void PostReplicatedChange(const struct FFastArraySerializer& InArraySerializer) { }

	/**
	 * Called when logging LogNetFastTArray (log or lower verbosity)
	 *
	 * @param InArraySerializer	Array serializer that owns the item and has triggered the replication call
	 * NOTE: intentionally not virtual; invoked via templated code, @see FExampleItemEntry
	 */
	FORCEINLINE FString GetDebugString() { return FString(TEXT("")); }
};

/** Struct for holding guid references */
struct FFastArraySerializerGuidReferences
{
	/** List of guids that were unmapped so we can quickly check */
	TSet<FNetworkGUID> UnmappedGUIDs;

	/** List of guids that were mapped so we can move them to unmapped when necessary (i.e. actor channel closes) */
	TSet<FNetworkGUID> MappedDynamicGUIDs;

	/** Buffer of data to re-serialize when the guids are mapped */
	TArray<uint8> Buffer;

	/** Number of bits in the buffer */
	int32 NumBufferBits;
};

/**
 * Struct used only in FFastArraySerializer::FastArrayDeltaSerialize, however, declaring it within the templated function
 * causes crashes on some clang compilers
 */
struct FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair
{
	FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair(int32 _idx, int32 _id) : Idx(_idx), ID(_id) { }
	int32	Idx;
	int32	ID;
};

UENUM()
enum class EFastArraySerializerDeltaFlags : uint8
{
	None,								//! No flags.
	HasBeenSerialized = 1 << 0,			//! Set when serialization at least once (i.e., this struct has been written or read).
	HasDeltaBeenRequested = 1 << 1,		//! Set if users requested Delta Serialization for this struct.
	IsUsingDeltaSerialization = 1 << 2,	//! This will remain unset until we've serialized at least once.
										//! At that point, this will be set if delta serialization was requested and
										//! we support it.
};
ENUM_CLASS_FLAGS(EFastArraySerializerDeltaFlags);

/** Base struct for wrapping the array used in Fast TArray Replication */
USTRUCT()
struct FFastArraySerializer
{
	GENERATED_USTRUCT_BODY()

	FFastArraySerializer()
		: IDCounter(0)
		, ArrayReplicationKey(0)
		, CachedNumItems(INDEX_NONE)
		, CachedNumItemsToConsiderForWriting(INDEX_NONE)
		, DeltaFlags(EFastArraySerializerDeltaFlags::None)
	{
		SetDeltaSerializationEnabled(true);
	}

	~FFastArraySerializer() {}

	/** Maps Element ReplicationID to Array Index.*/
	TMap<int32, int32> ItemMap;

	/** Counter used to assign IDs to new elements. */
	int32 IDCounter;

	/** Counter used to track array replication. */
	UPROPERTY(NotReplicated)
	int32 ArrayReplicationKey;

	/** List of items that need to be re-serialized when the referenced objects are mapped */
	TMap<int32, FFastArraySerializerGuidReferences> GuidReferencesMap;

	/** List of items that need to be re-serialized when the referenced objects are mapped.*/
	TMap<int32, FGuidReferencesMap> GuidReferencesMap_StructDelta;
	
	/** This must be called if you add or change an item in the array */
	void MarkItemDirty(FFastArraySerializerItem & Item)
	{
		if (Item.ReplicationID == INDEX_NONE)
		{
			Item.ReplicationID = ++IDCounter;
			if (IDCounter == INDEX_NONE)
			{
				IDCounter++;
			}
		}

		Item.ReplicationKey++;
		MarkArrayDirty();
	}

	/** This must be called if you just remove something from the array */
	void MarkArrayDirty()
	{
		ItemMap.Reset();		// This allows to clients to add predictive elements to arrays without affecting replication.
		IncrementArrayReplicationKey();

		// Invalidate the cached item counts so that they're recomputed during the next write
		CachedNumItems = INDEX_NONE;
		CachedNumItemsToConsiderForWriting = INDEX_NONE;
	}

	void IncrementArrayReplicationKey()
	{
		ArrayReplicationKey++;
		if (ArrayReplicationKey == INDEX_NONE)
		{
			ArrayReplicationKey++;
		}
	}

	/**
	 * Performs "standard" delta serialization on the items in the FastArraySerializer.
	 * This method relies more on the INetSerializeCB interface and custom logic and sends all properties
	 * that aren't marked as SkipRep, regardless of whether or not they've changed.
	 * This will be less CPU intensive, but require more bandwidth.
	 *
	 * @param Items				Array of items owned by ArraySerializer.
	 * @param Parms				Set of parms that dictate what serialization will do / return.
	 * @param ArraySerializer	The typed subclass of FFastArraySerializer that we're serializing.
	 */
	template<typename Type, typename SerializerType>
	static bool FastArrayDeltaSerialize(TArray<Type>& Items, FNetDeltaSerializeInfo& Parms, SerializerType& ArraySerializer);

	/**
	 * Called before removing elements and after the elements themselves are notified.  The indices are valid for this function call only!
	 *
	 * NOTE: intentionally not virtual; invoked via templated code, @see FExampleItemEntry
	 */
	FORCEINLINE void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize) { }

	/**
	 * Called after adding all new elements and after the elements themselves are notified.  The indices are valid for this function call only!
	 *
	 * NOTE: intentionally not virtual; invoked via templated code, @see FExampleItemEntry
	 */
	FORCEINLINE void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize) { }

	/**
	 * Called after updating all existing elements with new data and after the elements themselves are notified. The indices are valid for this function call only!
	 *
	 * NOTE: intentionally not virtual; invoked via templated code, @see FExampleItemEntry
	 */
	FORCEINLINE void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize) { }

	/**
	* Helper function for FastArrayDeltaSerialize to consolidate the logic of whether to consider writing an item in a fast TArray during network serialization.
	* For client replay recording, we don't want to write any items that have been added to the array predictively.
	*/
	template<typename Type, typename SerializerType>
	bool ShouldWriteFastArrayItem(const Type& Item, const bool bIsWritingOnClient) const
	{
		return !bIsWritingOnClient || Item.ReplicationID != INDEX_NONE;
	}

	void SetDeltaSerializationEnabled(const bool bEnabled)
	{
		if (!EnumHasAnyFlags(DeltaFlags, EFastArraySerializerDeltaFlags::HasBeenSerialized))
		{
			if (bEnabled)
			{
				DeltaFlags |= EFastArraySerializerDeltaFlags::HasDeltaBeenRequested;
			}
			else
			{
				DeltaFlags &= ~EFastArraySerializerDeltaFlags::HasDeltaBeenRequested;
			}
		}
		else
		{
			UE_LOG(LogNetFastTArray, Log, TEXT("FFastArraySerializer::SetDeltaSerializationEnabled - Called after array has been serialized. Ignoring"));
		}
	}

	const EFastArraySerializerDeltaFlags GetDeltaSerializationFlags() const
	{
		return DeltaFlags;
	}

	static const int32 GetMaxNumberOfAllowedChangesPerUpdate()
	{
		return MaxNumberOfAllowedChangesPerUpdate;
	}

	static const int32 GetMaxNumberOfAllowedDeletionsPerUpdate()
	{
		return MaxNumberOfAllowedDeletionsPerUpdate;
	}

private:

	ENGINE_API static int32 MaxNumberOfAllowedChangesPerUpdate;
	ENGINE_API static int32 MaxNumberOfAllowedDeletionsPerUpdate;
	ENGINE_API static FAutoConsoleVariableRef CVarMaxNumberOfAllowedChangesPerUpdate;
	ENGINE_API static FAutoConsoleVariableRef CVarMaxNumberOfAllowedDeletionsPerUpdate;

	/** Struct containing common header data that is written / read when serializing Fast Arrays. */
	struct FFastArraySerializerHeader
	{
		/** The current ArrayReplicationKey. */
		int32 ArrayReplicationKey;

		/** The previous ArrayReplicationKey. */
		int32 BaseReplicationKey;

		/** The number of changed elements (adds or removes). */
		int32 NumChanged;

		/**
		 * The list of deleted elements.
		 * When writing, this will be treated as IDs that are translated to indices prior to serialization.
		 * When reading, this will be actual indices.
		 */
		TArray<int32, TInlineAllocator<8>> DeletedIndices;
	};

	/**
	 * Helper struct that contains common methods / logic for standard Fast Array serialization and
	 * Delta Struct Fast Array serialization.
	 */
	template<typename Type, typename SerializerType>
	struct TFastArraySerializeHelper
	{
		/** Array element type struct. */
		UScriptStruct* Struct;

		/** Set of array elements we're serializing. */
		TArray<Type>& Items;

		/** The actual FFastArraySerializer struct we're serializing. */
		SerializerType& ArraySerializer;

		/** Cached DeltaSerialize params. */
		FNetDeltaSerializeInfo& Parms;

		/**
		 * Conditionally rebuilds the ID to Index map for items.
		 * This is generally only necessary on first serialization, or if we receive deletes and
		 * can no longer trust our ordering is correct.
		 */
		void ConditionalRebuildItemMap();

		/** Calculates the number of Items that actually need to be written. */
		int32 CalcNumItemsForConsideration() const;

		/** Conditionally logs the important state of the serializer. For debug purposes only. */
		void ConditionalLogSerializerState(const TMap<int32, int32>* OldIDToKeyMap) const;

		/**
		 * Checks to see if the ArrayReplicationKey has changed, and if so creates a new DeltaState that
		 * is passed out to the caller.
		 * Note, this state may just be a copy of a previous state, or a brand new state.
		 *
		 * @return	True if the keys were different and a state was created.
		 *			False if the keys were the same, and we can skip serialization.
		 */
		bool ConditionalCreateNewDeltaState(const TMap<int32, int32>& OldIDToKeyMap, const int32 BaseReplicationKey);

		/**
		 * Iterates over the current set of properties, comparing their keys with our old state, to figure
		 * out which have changed and need to be serialized. Also populates a list of elements that are
		 * no longer in our list (by ID).
		 */
		void BuildChangedAndDeletedBuffers(
			TMap<int32, int32>& NewIDToKeyMap,
			const TMap<int32, int32>* OldIDToKeyMap,
			TArray<FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair, TInlineAllocator<8>>& ChangedElements,
			TArray<int32, TInlineAllocator<8>>& DeletedElements);

		/** Writes out a FFastArraySerializerHeader */
		void WriteDeltaHeader(FFastArraySerializerHeader& Header) const;

		/** Reads in a FFastArraySerializerHeader */
		bool ReadDeltaHeader(FFastArraySerializerHeader& Header) const;

		/**
		 * Manages any cleanup work that needs to be done after receiving elements,
		 * such as looking for items that were implicitly deleted, removing all deleted items,
		 * firing off any PostReceive / PostDeleted events, etc.
		 */
		template<typename GuidMapType>
		void PostReceiveCleanup(
			FFastArraySerializerHeader& Header,
			TArray<int32, TInlineAllocator<8>>& ChangedIndices,
			TArray<int32, TInlineAllocator<8>>& AddedIndices,
			GuidMapType& GuidMap);
	};

	/**
	 * Performs "struct delta" serialization on the items in the FastArraySerializer.
	 * This method relies more directly on FRepLayout for management, and will only send properties
	 * that have changed since the last update.
	 * This is potentially more CPU intensive since we'll be doing comparisons, but should require less bandwidth.
	 *
	 * For this method to work, the following **must** be true:
	 *	- Your array of FFastArraySerializerItems must be a top level UPROPERTY within your FFastArraySerializer.
	 *	- Your array of FFastArraySerializerItems must **not** be marked RepSkip.
	 *	- Your array of FFastArraySerializerItems must be the **only** replicated array of FFastArraySerializerItems within the FastArraySerializer.
	 *		Note, it's OK to have multiple arrays of items, as long as only one is replicated (all others **must** be marked RepSkip).
	 *	- Your FFastArraySerializer must not be nested in a static array.
	 *	- Your array of FFastArraySerializerItem must not be nested in a static array.
	 *
	 * @param Items				Array of items owned by ArraySerializer.
	 * @param Parms				Set of parms that dictate what serialization will do / return.
	 * @param ArraySerializer	The typed subclass of FFastArraySerializer that we're serializing.
	 */
	template<typename Type, typename SerializerType>
	static bool FastArrayDeltaSerialize_DeltaSerializeStructs(TArray<Type>& Items, FNetDeltaSerializeInfo& Parms, SerializerType& ArraySerializer);

private:

	// Cached item counts, used for fast sanity checking when writing.
	int32 CachedNumItems;
	int32 CachedNumItemsToConsiderForWriting;

	UPROPERTY(NotReplicated, Transient)
	EFastArraySerializerDeltaFlags DeltaFlags;
};

template<typename Type, typename SerializerType>
void FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::ConditionalRebuildItemMap()
{
	if ((Parms.bUpdateUnmappedObjects || Parms.Writer == NULL) && ArraySerializer.ItemMap.Num() != Items.Num())
	{
		SCOPE_CYCLE_COUNTER(STAT_NetSerializeFastArray_BuildMap);
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize: Recreating Items map. Struct: %s, Items.Num: %d Map.Num: %d"), *Struct->GetOwnerStruct()->GetName(), Items.Num(), ArraySerializer.ItemMap.Num());
		ArraySerializer.ItemMap.Empty();
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i].ReplicationID == INDEX_NONE)
			{
				if (Parms.Writer)
				{
					UE_LOG(LogNetFastTArray, Warning, TEXT("FastArrayDeltaSerialize: Item with uninitialized ReplicationID. Struct: %s, ItemIndex: %i"), *Struct->GetOwnerStruct()->GetName(), i);
				}
				else
				{
					// This is benign for clients, they may add things to their local array without assigning a ReplicationID
					continue;
				}
			}

			ArraySerializer.ItemMap.Add(Items[i].ReplicationID, i);
		}
	}
}

template<typename Type, typename SerializerType>
int32 FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::CalcNumItemsForConsideration() const
{
	int32 Count = 0;

	// Count the number of items in the current array that may be written. On clients, items that were predicted will be skipped.
	for (const Type& Item : Items)
	{
		if (ArraySerializer.template ShouldWriteFastArrayItem<Type, SerializerType>(Item, Parms.bIsWritingOnClient))
		{
			Count++;
		}
	}

	return Count;
};

template<typename Type, typename SerializerType>
void FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::ConditionalLogSerializerState(const TMap<int32, int32>* OldIDToKeyMap) const
{
	// Log out entire state of current/base state
	if (UE_LOG_ACTIVE(LogNetFastTArray, Log))
	{
		FString CurrentState = FString::Printf(TEXT("Current: %d "), ArraySerializer.ArrayReplicationKey);
		for (const Type& Item : Items)
		{
			CurrentState += FString::Printf(TEXT("[%d/%d], "), Item.ReplicationID, Item.ReplicationKey);
		}
		UE_LOG(LogNetFastTArray, Log, TEXT("%s"), *CurrentState);


		FString ClientStateStr = FString::Printf(TEXT("Client: %d "), Parms.OldState ? ((FNetFastTArrayBaseState*)Parms.OldState)->ArrayReplicationKey : 0);
		if (OldIDToKeyMap)
		{
			for (const TPair<int32, int32>& KeyValuePair : *OldIDToKeyMap)
			{
				ClientStateStr += FString::Printf(TEXT("[%d/%d], "), KeyValuePair.Key, KeyValuePair.Value);
			}
		}
		UE_LOG(LogNetFastTArray, Log, TEXT("%s"), *ClientStateStr);
	}
}

template<typename Type, typename SerializerType>
bool FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::ConditionalCreateNewDeltaState(const TMap<int32, int32>& OldIDToKeyMap, const int32 BaseReplicationKey)
{
	if (ArraySerializer.ArrayReplicationKey == BaseReplicationKey)
	{
		// If the keys didn't change, only update the item count caches if necessary.
		if (ArraySerializer.CachedNumItems == INDEX_NONE ||
			ArraySerializer.CachedNumItems != Items.Num() ||
			ArraySerializer.CachedNumItemsToConsiderForWriting == INDEX_NONE)
		{
			ArraySerializer.CachedNumItems = Items.Num();
			ArraySerializer.CachedNumItemsToConsiderForWriting = CalcNumItemsForConsideration();
		}

		if (UNLIKELY(OldIDToKeyMap.Num() != ArraySerializer.CachedNumItemsToConsiderForWriting))
		{
			UE_LOG(LogNetFastTArray, Warning, TEXT("OldMap size (%d) does not match item count (%d)"), OldIDToKeyMap.Num(), ArraySerializer.CachedNumItemsToConsiderForWriting);
		}

		if (Parms.OldState)
		{
			// Nothing changed and we had a valid old state, so just use/share the existing state. No need to create a new one.
			*Parms.NewState = Parms.OldState->AsShared();
		}
		else
		{
			// Nothing changed but we don't have an existing state of our own yet so we need to make one here.
			FNetFastTArrayBaseState* NewState = new FNetFastTArrayBaseState();
			*Parms.NewState = MakeShareable(NewState);
			NewState->ArrayReplicationKey = ArraySerializer.ArrayReplicationKey;
		}
		
		return false;
	}

	return true;
}

template<typename Type, typename SerializerType>
void FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::BuildChangedAndDeletedBuffers(
	TMap<int32, int32>& NewIDToKeyMap,
	const TMap<int32, int32>* OldIDToKeyMap,
	TArray<FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair, TInlineAllocator<8>>& ChangedElements,
	TArray<int32, TInlineAllocator<8>>& DeletedElements)
{
	ConditionalLogSerializerState(OldIDToKeyMap);

	const int32 NumConsideredItems = CalcNumItemsForConsideration();

	int32 DeleteCount = (OldIDToKeyMap ? OldIDToKeyMap->Num() : 0) - NumConsideredItems; // Note: this is incremented when we add new items below.
	UE_LOG(LogNetFastTArray, Log, TEXT("NetSerializeItemDeltaFast: %s. DeleteCount: %d"), *Parms.DebugName, DeleteCount);

	//--------------------------------------------
	// Find out what is new or what has changed
	//--------------------------------------------
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		Type& Item = Items[i];

		UE_LOG(LogNetFastTArray, Log, TEXT("    Array[%d] - ID %d. CL %d."), i, Item.ReplicationID, Item.ReplicationKey);
		if (!ArraySerializer.template ShouldWriteFastArrayItem<Type, SerializerType>(Item, Parms.bIsWritingOnClient))
		{
			// On clients, this will skip items that were added predictively.
			continue;
		}
		if (Item.ReplicationID == INDEX_NONE)
		{
			ArraySerializer.MarkItemDirty(Item);
		}
		NewIDToKeyMap.Add(Item.ReplicationID, Item.ReplicationKey);

		const int32* OldValuePtr = OldIDToKeyMap ? OldIDToKeyMap->Find(Item.ReplicationID) : NULL;
		if (OldValuePtr)
		{
			if (*OldValuePtr == Item.ReplicationKey)
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("       Stayed The Same - Skipping"));

				// Stayed the same, it might have moved but we dont care
				continue;
			}
			else
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("       Changed! Was: %d. Element ID: %d. %s"), *OldValuePtr, Item.ReplicationID, *Item.GetDebugString());

				// Changed
				ChangedElements.Add(FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair(i, Item.ReplicationID));
			}
		}
		else
		{
			UE_LOG(LogNetFastTArray, Log, TEXT("       New! Element ID: %d. %s"), Item.ReplicationID, *Item.GetDebugString());

			// The item really should have a valid ReplicationID but in the case of loading from a save game,
			// items may not have been marked dirty individually. Its ok to just assign them one here.
			// New
			ChangedElements.Add(FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair(i, Item.ReplicationID));
			++DeleteCount; // We added something new, so our initial DeleteCount value must be incremented.
		}
	}

	// Find out what was deleted
	if (DeleteCount > 0 && OldIDToKeyMap)
	{
		for (auto It = OldIDToKeyMap->CreateConstIterator(); It; ++It)
		{
			if (!NewIDToKeyMap.Contains(It.Key()))
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("   Deleting ID: %d"), It.Key());

				DeletedElements.Add(It.Key());
				if (--DeleteCount <= 0)
				{
					break;
				}
			}
		}
	}
}

template<typename Type, typename SerializerType>
void FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::WriteDeltaHeader(FFastArraySerializerHeader& Header) const
{
	FBitWriter& Writer = *Parms.Writer;

	Writer << Header.ArrayReplicationKey;
	Writer << Header.BaseReplicationKey;

	int32 NumDeletes = Header.DeletedIndices.Num();
	Writer << NumDeletes;

	Writer << Header.NumChanged;

	UE_LOG(LogNetFastTArray, Log, TEXT("   Writing Bunch. NumChange: %d. NumDel: %d [%d/%d]"),
		Header.NumChanged, Header.DeletedIndices.Num(), Header.ArrayReplicationKey, Header.BaseReplicationKey);

	const int32 MaxNumDeleted = FFastArraySerializer::GetMaxNumberOfAllowedDeletionsPerUpdate();
	const int32 MaxNumChanged = FFastArraySerializer::GetMaxNumberOfAllowedChangesPerUpdate();
	
	// TODO: We should consider propagating this error in the same way we handle
	// array overflows in RepLayout SendProperties / CompareProperties.
	UE_CLOG(NumDeletes > MaxNumDeleted, LogNetFastTArray, Warning, TEXT("NumDeletes > GetMaxNumberOfAllowedDeletionsPerUpdate: %d > %d. (Write)"), NumDeletes, MaxNumDeleted);
	UE_CLOG(Header.NumChanged > MaxNumChanged, LogNetFastTArray, Warning, TEXT("NumChanged > GetMaxNumberOfAllowedChangesPerUpdate: %d > %d. (Write)"), Header.NumChanged, MaxNumChanged);

	// Serialize deleted items, just by their ID
	for (auto It = Header.DeletedIndices.CreateIterator(); It; ++It)
	{
		int32 ID = *It;
		Writer << ID;
		UE_LOG(LogNetFastTArray, Log, TEXT("   Deleted ElementID: %d"), ID);
	}
}

template<typename Type, typename SerializerType>
bool FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::ReadDeltaHeader(FFastArraySerializerHeader& Header) const
{
	FBitReader& Reader = *Parms.Reader;

	//---------------
	// Read header
	//---------------

	Reader << Header.ArrayReplicationKey;
	Reader << Header.BaseReplicationKey;

	int32 NumDeletes = 0;
	Reader << NumDeletes;

	UE_LOG(LogNetFastTArray, Log, TEXT("Received [%d/%d]."), Header.ArrayReplicationKey, Header.BaseReplicationKey);

	const int32 MaxNumDeleted = FFastArraySerializer::GetMaxNumberOfAllowedDeletionsPerUpdate();
	if (NumDeletes > MaxNumDeleted)
	{
		UE_LOG(LogNetFastTArray, Warning, TEXT("NumDeletes > GetMaxNumberOfAllowedDeletionsPerUpdate: %d > %d. (Read)"), NumDeletes, MaxNumDeleted);
		Reader.SetError();
		return false;
	}

	Reader << Header.NumChanged;

	const int32 MaxNumChanged = FFastArraySerializer::GetMaxNumberOfAllowedChangesPerUpdate();
	if (Header.NumChanged > MaxNumChanged)
	{
		UE_LOG(LogNetFastTArray, Warning, TEXT("NumChanged > GetMaxNumberOfAllowedChangesPerUpdate: %d > %d. (Read)"), Header.NumChanged, MaxNumChanged);
		Reader.SetError();
		return false;
	}

	UE_LOG(LogNetFastTArray, Log, TEXT("Read NumChanged: %d NumDeletes: %d."), Header.NumChanged, NumDeletes);

	//---------------
	// Read deleted elements
	//---------------
	if (NumDeletes > 0)
	{
		for (int32 i = 0; i < NumDeletes; ++i)
		{
			int32 ElementID = 0;
			Reader << ElementID;

			int32* ElementIndexPtr = ArraySerializer.ItemMap.Find(ElementID);
			if (ElementIndexPtr)
			{
				int32 DeleteIndex = *ElementIndexPtr;
				Header.DeletedIndices.Add(DeleteIndex);
				UE_LOG(LogNetFastTArray, Log, TEXT("   Adding ElementID: %d for deletion"), ElementID);
			}
			else
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("   Couldn't find ElementID: %d for deletion!"), ElementID);
			}
		}
	}

	return true;
}

template<typename Type, typename SerializerType>
template<typename GuidMapType>
void FFastArraySerializer::TFastArraySerializeHelper<Type, SerializerType>::PostReceiveCleanup(
	FFastArraySerializerHeader& Header,
	TArray<int32, TInlineAllocator<8>>& ChangedIndices,
	TArray<int32, TInlineAllocator<8>>& AddedIndices,
	GuidMapType& GuidMap)
{
	// ---------------------------------------------------------
	// Look for implicit deletes that would happen due to Naks
	// ---------------------------------------------------------

	// If we're sending data completely reliably, there's no need to do this.
	if (!Parms.bInternalAck)
	{
		for (int32 idx = 0; idx < Items.Num(); ++idx)
		{
			Type& Item = Items[idx];
			if (Item.MostRecentArrayReplicationKey < Header.ArrayReplicationKey && Item.MostRecentArrayReplicationKey > Header.BaseReplicationKey)
			{
				// Make sure this wasn't an explicit delete in this bunch (otherwise we end up deleting an extra element!)
				if (!Header.DeletedIndices.Contains(idx))
				{
					// This will happen in normal conditions in network replays.
					UE_LOG(LogNetFastTArray, Log, TEXT("Adding implicit delete for ElementID: %d. MostRecentArrayReplicationKey: %d. Current Payload: [%d/%d]"),
						Item.ReplicationID, Item.MostRecentArrayReplicationKey, Header.ArrayReplicationKey, Header.BaseReplicationKey);

					Header.DeletedIndices.Add(idx);
				}
			}
		}
	}

	// Increment keys so that a client can re-serialize the array if needed, such as for client replay recording.
	// Must check the size of DeleteIndices instead of NumDeletes to handle implicit deletes.
	if (Header.DeletedIndices.Num() > 0 || Header.NumChanged > 0)
	{
		ArraySerializer.IncrementArrayReplicationKey();
	}

	// ---------------------------------------------------------
	// Invoke all callbacks: removed -> added -> changed
	// ---------------------------------------------------------

	int32 PreRemoveSize = Items.Num();
	int32 FinalSize = PreRemoveSize - Header.DeletedIndices.Num();
	for (int32 idx : Header.DeletedIndices)
	{
		if (Items.IsValidIndex(idx))
		{
			// Remove the deleted element's tracked GUID references
			if (GuidMap.Remove(Items[idx].ReplicationID) > 0)
			{
				Parms.bGuidListsChanged = true;
			}

			// Call the delete callbacks now, actually remove them at the end
			Items[idx].PreReplicatedRemove(ArraySerializer);
		}
	}
	ArraySerializer.PreReplicatedRemove(Header.DeletedIndices, FinalSize);

	if (PreRemoveSize != Items.Num())
	{
		UE_LOG(LogNetFastTArray, Error, TEXT("Item size changed after PreReplicatedRemove! PremoveSize: %d  Item.Num: %d"),
			PreRemoveSize, Items.Num());
	}

	for (int32 idx : AddedIndices)
	{
		Items[idx].PostReplicatedAdd(ArraySerializer);
	}
	ArraySerializer.PostReplicatedAdd(AddedIndices, FinalSize);

	for (int32 idx : ChangedIndices)
	{
		Items[idx].PostReplicatedChange(ArraySerializer);
	}
	ArraySerializer.PostReplicatedChange(ChangedIndices, FinalSize);

	if (PreRemoveSize != Items.Num())
	{
		UE_LOG(LogNetFastTArray, Error, TEXT("Item size changed after PostReplicatedAdd/PostReplicatedChange! PremoveSize: %d  Item.Num: %d"),
			PreRemoveSize, Items.Num());
	}

	if (Header.DeletedIndices.Num() > 0)
	{
		Header.DeletedIndices.Sort();
		for (int32 i = Header.DeletedIndices.Num() - 1; i >= 0; --i)
		{
			int32 DeleteIndex = Header.DeletedIndices[i];
			if (Items.IsValidIndex(DeleteIndex))
			{
				Items.RemoveAtSwap(DeleteIndex, 1, false);

				UE_LOG(LogNetFastTArray, Log, TEXT("   Deleting: %d"), DeleteIndex);
			}
		}

		// Clear the map now that the indices are all shifted around. This kind of sucks, we could use slightly better data structures here I think.
		// This will force the ItemMap to be rebuilt for the current Items array
		ArraySerializer.ItemMap.Empty();
	}
}

/** The function that implements Fast TArray Replication  */
template<typename Type, typename SerializerType >
bool FFastArraySerializer::FastArrayDeltaSerialize(TArray<Type> &Items, FNetDeltaSerializeInfo& Parms, SerializerType& ArraySerializer)
{
	// It's possible that we end up calling this method on clients before we've actually received
	// anything from the server (Net Conditions, Static Actors, etc.)
	// That should be fine though, because none of the GUID Tracking work should actually do anything
	// until after we've received.
	if (EnumHasAllFlags(ArraySerializer.DeltaFlags, EFastArraySerializerDeltaFlags::IsUsingDeltaSerialization))
	{
		return FastArrayDeltaSerialize_DeltaSerializeStructs(Items, Parms, ArraySerializer);
	}

	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_NetSerializeFastArray, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0);
	class UScriptStruct* InnerStruct = Type::StaticStruct();

	UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize for %s. %s. %s"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName(), Parms.Reader ? TEXT("Reading") : TEXT("Writing"));

	TFastArraySerializeHelper<Type, SerializerType> Helper{
		InnerStruct,
		Items,
		ArraySerializer,
		Parms
	};

	//---------------
	// Build ItemMap if necessary. This maps ReplicationID to our local index into the Items array.
	//---------------
	Helper.ConditionalRebuildItemMap();

	if ( Parms.GatherGuidReferences )
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize for %s. %s. GatherGuidReferences"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());

		// Loop over all tracked guids, and return what we have
		for ( const auto& GuidReferencesPair : ArraySerializer.GuidReferencesMap )
		{
			const FFastArraySerializerGuidReferences& GuidReferences = GuidReferencesPair.Value;

			Parms.GatherGuidReferences->Append( GuidReferences.UnmappedGUIDs );
			Parms.GatherGuidReferences->Append( GuidReferences.MappedDynamicGUIDs );

			if ( Parms.TrackedGuidMemoryBytes )
			{
				*Parms.TrackedGuidMemoryBytes += GuidReferences.Buffer.Num();
			}
		}

		return true;
	}

	if ( Parms.MoveGuidToUnmapped )
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize for %s. %s. MovedGuidToUnmapped"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());

		bool bFound = false;

		const FNetworkGUID GUID = *Parms.MoveGuidToUnmapped;

		// Try to find the guid in the list, and make sure it's on the unmapped lists now
		for ( auto& GuidReferencesPair : ArraySerializer.GuidReferencesMap )
		{
			FFastArraySerializerGuidReferences& GuidReferences = GuidReferencesPair.Value;

			if ( GuidReferences.MappedDynamicGUIDs.Contains( GUID ) )
			{
				GuidReferences.MappedDynamicGUIDs.Remove( GUID );
				GuidReferences.UnmappedGUIDs.Add( GUID );
				bFound = true;
			}
		}

		return bFound;
	}

	if ( Parms.bUpdateUnmappedObjects )
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize for %s. %s. UpdateUnmappedObjects"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());

		// Loop over each item that has unmapped objects
		for ( auto It = ArraySerializer.GuidReferencesMap.CreateIterator(); It; ++It )
		{
			// Get the element id
			const int32 ElementID = It.Key();
			
			// Get a reference to the unmapped item itself
			FFastArraySerializerGuidReferences& GuidReferences = It.Value();

			if ( ( GuidReferences.UnmappedGUIDs.Num() == 0 && GuidReferences.MappedDynamicGUIDs.Num() == 0 ) || ArraySerializer.ItemMap.Find( ElementID ) == NULL )
			{
				// If for some reason the item is gone (or all guids were removed), we don't need to track guids for this item anymore
				It.RemoveCurrent();
				continue;		// We're done with this unmapped item
			}

			// Loop over all the guids, and check to see if any of them are loaded yet
			bool bMappedSomeGUIDs = false;

			for ( auto UnmappedIt = GuidReferences.UnmappedGUIDs.CreateIterator(); UnmappedIt; ++UnmappedIt )
			{
				const FNetworkGUID& GUID = *UnmappedIt;

				if ( Parms.Map->IsGUIDBroken( GUID, false ) )
				{
					// Stop trying to load broken guids
					UE_LOG( LogNetFastTArray, Warning, TEXT( "FastArrayDeltaSerialize: Broken GUID. NetGuid: %s" ), *GUID.ToString() );
					UnmappedIt.RemoveCurrent();
					continue;
				}

				UObject* Object = Parms.Map->GetObjectFromNetGUID( GUID, false );

				if ( Object != NULL )
				{
					// This guid loaded!
					if ( GUID.IsDynamic() )
					{
						GuidReferences.MappedDynamicGUIDs.Add( GUID );		// Move back to mapped list
					}
					UnmappedIt.RemoveCurrent();
					bMappedSomeGUIDs = true;
				}
			}

			// Check to see if we loaded any guids. If we did, we can serialize the element again which will load it this time
			if ( bMappedSomeGUIDs )
			{
				Parms.bOutSomeObjectsWereMapped = true;

				if ( !Parms.bCalledPreNetReceive )
				{
					// Call PreNetReceive if we are going to change a value (some game code will need to think this is an actual replicated value)
					Parms.Object->PreNetReceive();
					Parms.bCalledPreNetReceive = true;
				}

				Type* ThisElement = &Items[ArraySerializer.ItemMap.FindChecked( ElementID )];

				// Initialize the reader with the stored buffer that we need to read from
				FNetBitReader Reader( Parms.Map, GuidReferences.Buffer.GetData(), GuidReferences.NumBufferBits );

				// Read the property (which should serialize any newly mapped objects as well)
				Parms.Struct = InnerStruct;
				Parms.Data = ThisElement;
				Parms.Reader = &Reader;
				Parms.NetSerializeCB->NetSerializeStruct(Parms);

				// Let the element know it changed
				ThisElement->PostReplicatedChange(ArraySerializer);
			}

			// If we have no more guids, we can remove this item for good
			if ( GuidReferences.UnmappedGUIDs.Num() == 0 && GuidReferences.MappedDynamicGUIDs.Num() == 0 )
			{
				It.RemoveCurrent();
			}
		}

		// If we still have unmapped items, then communicate this to the outside
		if ( ArraySerializer.GuidReferencesMap.Num() > 0 )
		{
			Parms.bOutHasMoreUnmapped = true;
		}

		return true;
	}

	// If we've made it this far, it means that we're going to be serializing something.
	// So, it should be safe for us to update our cached state.
	// Also, make sure that we hit the right path if we need to.
	if (!EnumHasAnyFlags(ArraySerializer.DeltaFlags, EFastArraySerializerDeltaFlags::HasBeenSerialized))
	{
		ArraySerializer.DeltaFlags |= EFastArraySerializerDeltaFlags::HasBeenSerialized;
		if (Parms.bSupportsFastArrayDeltaStructSerialization && EnumHasAnyFlags(ArraySerializer.DeltaFlags, EFastArraySerializerDeltaFlags::HasDeltaBeenRequested))
		{
			ArraySerializer.DeltaFlags |= EFastArraySerializerDeltaFlags::IsUsingDeltaSerialization;
			return FastArrayDeltaSerialize_DeltaSerializeStructs(Items, Parms, ArraySerializer);
		}
	}

	if (Parms.Writer)
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize for %s. %s. Writing"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());

		//-----------------------------
		// Saving
		//-----------------------------	
		check(Parms.Struct);
		FBitWriter& Writer = *Parms.Writer;

		// Get the old map if its there
		TMap<int32, int32> * OldMap = nullptr;
		int32 BaseReplicationKey = INDEX_NONE;

		// See if the array changed at all. If the ArrayReplicationKey matches we can skip checking individual items
		if (Parms.OldState)
		{
			OldMap = &((FNetFastTArrayBaseState*)Parms.OldState)->IDToCLMap;
			BaseReplicationKey = ((FNetFastTArrayBaseState*)Parms.OldState)->ArrayReplicationKey;

			// If we didn't create a new delta state, that implies nothing changed,
			// so we're done.
			if (!Helper.ConditionalCreateNewDeltaState(*OldMap, BaseReplicationKey))
			{
				return false;
			}
		}

		// Create a new map from the current state of the array		
		FNetFastTArrayBaseState * NewState = new FNetFastTArrayBaseState();

		check(Parms.NewState);
		*Parms.NewState = MakeShareable( NewState );
		TMap<int32, int32> & NewMap = NewState->IDToCLMap;
		NewState->ArrayReplicationKey = ArraySerializer.ArrayReplicationKey;

		FFastArraySerializerHeader Header{
			ArraySerializer.ArrayReplicationKey,
			BaseReplicationKey,
			0
		};

		TArray<FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair, TInlineAllocator<8>> ChangedElements;

		Helper.BuildChangedAndDeletedBuffers(NewMap, OldMap, ChangedElements, Header.DeletedIndices);
		
		// Note: we used to early return false here if nothing had changed, but we still need to send
		// a bunch with the array key / base key, so that clients can look for implicit deletes.

		// The array replication key may have changed while adding new elements (in the call to MarkItemDirty above)
		NewState->ArrayReplicationKey = ArraySerializer.ArrayReplicationKey;

		//----------------------
		// Write it out.
		//----------------------

		Header.NumChanged = ChangedElements.Num();
		Helper.WriteDeltaHeader(Header);

		// Serialized new elements with their payload
		for (auto It = ChangedElements.CreateIterator(); It; ++It)
		{
			void* ThisElement = &Items[It->Idx];

			// Dont pack this, want property to be byte aligned
			uint32 ID = It->ID;
			Writer << ID;

			UE_LOG(LogNetFastTArray, Log, TEXT("   Changed ElementID: %d"), ID);

			Parms.Struct = InnerStruct;
			Parms.Data = ThisElement;
			Parms.NetSerializeCB->NetSerializeStruct(Parms);
		}
	}
	else
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize for %s. %s. Reading"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());

		//-----------------------------
		// Loading
		//-----------------------------	
		check(Parms.Reader);
		FBitReader& Reader = *Parms.Reader;

		FFastArraySerializerHeader Header;
		if (!Helper.ReadDeltaHeader(Header))
		{
			return false;
		}

		TArray<int32, TInlineAllocator<8>> ChangedIndices;
		TArray<int32, TInlineAllocator<8>> AddedIndices;

		//---------------
		// Read Changed/New elements
		//---------------
		for(int32 i = 0; i < Header.NumChanged; ++i)
		{
			int32 ElementID = 0;
			Reader << ElementID;

			int32* ElementIndexPtr = ArraySerializer.ItemMap.Find(ElementID);
			int32 ElementIndex = 0;
			Type* ThisElement = nullptr;
		
			if (!ElementIndexPtr)
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("   New. ID: %d. New Element!"), ElementID);

				ThisElement = new (Items)Type();
				ThisElement->ReplicationID = ElementID;

				ElementIndex = Items.Num()-1;
				ArraySerializer.ItemMap.Add(ElementID, ElementIndex);
				
				AddedIndices.Add(ElementIndex);
			}
			else
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("   Changed. ID: %d -> Idx: %d"), ElementID, *ElementIndexPtr);
				ElementIndex = *ElementIndexPtr;
				ThisElement = &Items[ElementIndex];
				ChangedIndices.Add(ElementIndex);
			}

			// Update this element's most recent array replication key
			ThisElement->MostRecentArrayReplicationKey = Header.ArrayReplicationKey;

			// Update this element's replication key so that a client can re-serialize the array for client replay recording
			ThisElement->ReplicationKey++;

			// Let package map know we want to track and know about any guids that are unmapped during the serialize call
			Parms.Map->ResetTrackedGuids( true );

			// Remember where we started reading from, so that if we have unmapped properties, we can re-deserialize from this data later
			FBitReaderMark Mark( Reader );

			Parms.Struct = InnerStruct;
			Parms.Data = ThisElement;
			Parms.NetSerializeCB->NetSerializeStruct(Parms);

			if ( !Reader.IsError() )
			{
				// Track unmapped guids
				const TSet< FNetworkGUID >& TrackedUnmappedGuids = Parms.Map->GetTrackedUnmappedGuids();
				const TSet< FNetworkGUID >& TrackedMappedDynamicGuids = Parms.Map->GetTrackedDynamicMappedGuids();

				if ( TrackedUnmappedGuids.Num() || TrackedMappedDynamicGuids.Num() )
				{	
					FFastArraySerializerGuidReferences& GuidReferences = ArraySerializer.GuidReferencesMap.FindOrAdd( ElementID );

					// If guid lists are different, make note of that, and copy respective list
					if ( !NetworkGuidSetsAreSame( GuidReferences.UnmappedGUIDs, TrackedUnmappedGuids ) )
					{
						// Copy the unmapped guid list to this unmapped item
						GuidReferences.UnmappedGUIDs = TrackedUnmappedGuids;
						Parms.bGuidListsChanged = true;
					}

					if ( !NetworkGuidSetsAreSame( GuidReferences.MappedDynamicGUIDs, TrackedMappedDynamicGuids ) )
					{ 
						// Copy the mapped guid list
						GuidReferences.MappedDynamicGUIDs = TrackedMappedDynamicGuids;
						Parms.bGuidListsChanged = true;
					}

					GuidReferences.Buffer.Empty();

					// Remember the number of bits in the buffer
					GuidReferences.NumBufferBits = Reader.GetPosBits() - Mark.GetPos();
					
					// Copy the buffer itself
					Mark.Copy( Reader, GuidReferences.Buffer );

					// Hijack this property to communicate that we need to be tracked since we have some unmapped guids
					if ( TrackedUnmappedGuids.Num() )
					{
						Parms.bOutHasMoreUnmapped = true;
					}
				}
				else
				{
					// If we don't have any unmapped objects, make sure we're no longer tracking this item in the unmapped lists
					ArraySerializer.GuidReferencesMap.Remove( ElementID );
				}
			}

			// Stop tracking unmapped objects
			Parms.Map->ResetTrackedGuids( false );

			if ( Reader.IsError() )
			{
				UE_LOG( LogNetFastTArray, Warning, TEXT( "Parms.NetSerializeCB->NetSerializeStruct: Reader.IsError() == true" ) );
				return false;
			}
		}

		Helper.template PostReceiveCleanup<>(Header, ChangedIndices, AddedIndices, ArraySerializer.GuidReferencesMap);
	}

	return true;
}

struct FFastArrayDeltaSerializeParams
{
	FNetDeltaSerializeInfo& DeltaSerializeInfo;
	FFastArraySerializer& ArraySerializer;

	const TFunction<void(void*, const FFastArrayDeltaSerializeParams&)> PreReplicatedRemove;
	const TFunction<void(void*, const FFastArrayDeltaSerializeParams&)> PostReplicatedAdd;
	const TFunction<void(void*, const FFastArrayDeltaSerializeParams&)> PostReplicatedChange;
	const TFunction<void(void*, const FFastArrayDeltaSerializeParams&, const uint32)> ReceivedItem;

	TArray<FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair, TInlineAllocator<8>>* WriteChangedElements = nullptr;
	FNetFastTArrayBaseState* WriteBaseState = nullptr;
	TArray<int32, TInlineAllocator<8>>* ReadChangedElements = nullptr;
	TArray<int32, TInlineAllocator<8>>* ReadAddedElements = nullptr;
	int32 ReadNumChanged = INDEX_NONE;
	int32 ReadArrayReplicationKey = INDEX_NONE;
};

template<typename Type, typename SerializerType>
bool FFastArraySerializer::FastArrayDeltaSerialize_DeltaSerializeStructs(TArray<Type>& Items, FNetDeltaSerializeInfo& Parms, SerializerType& ArraySerializer)
{
	/**
	 * These methods are exposed on FastArraySerializerItems, but they aren't virtual.
	 * Further, we may not know the exact type when we want to call them, and won't
	 * safely be able to cast to the type in non-templated code.
	 *
	 * Maybe this defeats the purpose of having them not virtual in the first place.
	 * However, for now ReceivedItem and PostReplicatedChanged are the only ones that will actually
	 * be called in this way, whereas PostReplicatedAdd and PreReplicatedRemove will still be called
	 * from templated code.
	 */
	struct FFastArrayItemCallbackHelper
	{
		static void PreReplicatedRemove(void* FastArrayItem, const struct FFastArrayDeltaSerializeParams& Params)
		{
			reinterpret_cast<Type*>(FastArrayItem)->PreReplicatedRemove(static_cast<SerializerType&>(Params.ArraySerializer));
		}

		static void PostReplicatedAdd(void* FastArrayItem, const struct FFastArrayDeltaSerializeParams& Params)
		{
			reinterpret_cast<Type*>(FastArrayItem)->PostReplicatedAdd(static_cast<SerializerType&>(Params.ArraySerializer));
		}

		static void PostReplicatedChange(void* FastArrayItem, const struct FFastArrayDeltaSerializeParams& Params)
		{
			reinterpret_cast<Type*>(FastArrayItem)->PostReplicatedChange(static_cast<SerializerType&>(Params.ArraySerializer));
		}

		static void ReceivedItem(void* FastArrayItem, const FFastArrayDeltaSerializeParams& Params, const uint32 ReplicationID)
		{
			Type* Item = reinterpret_cast<Type*>(FastArrayItem);
			Item->ReplicationID = ReplicationID;
			Item->MostRecentArrayReplicationKey = Params.ReadArrayReplicationKey;
			Item->ReplicationKey++;
		}
	};

	CONDITIONAL_SCOPE_CYCLE_COUNTER(STAT_NetSerializeFastArray_DeltaStruct, CVarNetEnableDetailedScopeCounters.GetValueOnAnyThread() > 0);

	class UScriptStruct* InnerStruct = Type::StaticStruct();

	TFastArraySerializeHelper<Type, SerializerType> Helper{
		InnerStruct,
		Items,
		ArraySerializer,
		Parms
	};

	FFastArrayDeltaSerializeParams DeltaSerializeParams{
		Parms,
		ArraySerializer,
		FFastArrayItemCallbackHelper::PreReplicatedRemove,
		FFastArrayItemCallbackHelper::PostReplicatedAdd,
		FFastArrayItemCallbackHelper::PostReplicatedChange,
		FFastArrayItemCallbackHelper::ReceivedItem,
	};

	//---------------
	// Build ItemMap if necessary. This maps ReplicationID to our local index into the Items array.
	//---------------
	Helper.ConditionalRebuildItemMap();

	if (Parms.GatherGuidReferences)
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize_DeltaSerializeStruct for %s. %s. GatherGuidReferences"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());
		Parms.NetSerializeCB->GatherGuidReferencesForFastArray(DeltaSerializeParams);
		return true;
	}
	else if (Parms.MoveGuidToUnmapped)
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize_DeltaSerializeStruct for %s. %s. MoveGuidToUnmapped"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());
		return Parms.NetSerializeCB->MoveGuidToUnmappedForFastArray(DeltaSerializeParams);
	}
	else if (Parms.bUpdateUnmappedObjects)
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize_DeltaSerializeStruct for %s. %s. UpdateUnmappedObjects"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());
		Parms.NetSerializeCB->UpdateUnmappedGuidsForFastArray(DeltaSerializeParams);
		return true;
	}
	else if (Parms.Writer)
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize_DeltaSerializeStruct for %s. %s. Writing"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());

		//-----------------------------
		// Saving
		//-----------------------------	
		check(Parms.Struct);
		FBitWriter& Writer = *Parms.Writer;

		// Get the old map if its there
		TMap<int32, int32>* OldItemMap = nullptr;
		int32 BaseReplicationKey = INDEX_NONE;
		int32 OldChangelistHistory = INDEX_NONE;
		int32 OldLastAckedHistory = INDEX_NONE;

		// See if the array changed at all. If the ArrayReplicationKey matches we can skip checking individual items
		if (Parms.OldState)
		{
			OldItemMap = &((FNetFastTArrayBaseState*)Parms.OldState)->IDToCLMap;
			BaseReplicationKey = ((FNetFastTArrayBaseState*)Parms.OldState)->ArrayReplicationKey;
			OldChangelistHistory = Parms.OldState->GetChangelistHistory();
			OldLastAckedHistory = Parms.OldState->GetLastAckedHistory();

			if (!Helper.ConditionalCreateNewDeltaState(*OldItemMap, BaseReplicationKey))
			{
				return false;
			}
		}

		// Create a new map from the current state of the array		
		FNetFastTArrayBaseState* NewState = new FNetFastTArrayBaseState();

		check(Parms.NewState);

		*Parms.NewState = MakeShareable(NewState);
		NewState->ArrayReplicationKey = ArraySerializer.ArrayReplicationKey;

		TMap<int32, int32>& NewItemMap = NewState->IDToCLMap;

		FFastArraySerializerHeader Header{
			ArraySerializer.ArrayReplicationKey,
			BaseReplicationKey,
			0
		};

		TArray<FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair, TInlineAllocator<8>> ChangedElements;
		Helper.BuildChangedAndDeletedBuffers(NewItemMap, OldItemMap, ChangedElements, Header.DeletedIndices);

		// Note: we used to early return false here if nothing had changed, but we still need to send
		// a bunch with the array key / base key, so that clients can look for implicit deletes.

		// The array replication key may have changed while adding new elemnts (in the call to MarkItemDirty above)
		NewState->ArrayReplicationKey = ArraySerializer.ArrayReplicationKey;

		//----------------------
		// Write it out.
		//----------------------

		Header.NumChanged = ChangedElements.Num();
		Helper.WriteDeltaHeader(Header);

		NewState->SetChangelistHistory(OldChangelistHistory);
		NewState->SetLastAckedHistory(OldLastAckedHistory);

		DeltaSerializeParams.WriteChangedElements = &ChangedElements;
		DeltaSerializeParams.WriteBaseState = NewState;

		return Parms.NetSerializeCB->NetDeltaSerializeForFastArray(DeltaSerializeParams);
	}
	else
	{
		UE_LOG(LogNetFastTArray, Log, TEXT("FastArrayDeltaSerialize_DeltaSerializeStruct for %s. %s. Reading"), *InnerStruct->GetName(), *InnerStruct->GetOwnerStruct()->GetName());

		//-----------------------------
		// Loading
		//-----------------------------	
		check(Parms.Reader);
		FBitReader& Reader = *Parms.Reader;

		FFastArraySerializerHeader Header;
		if (!Helper.ReadDeltaHeader(Header))
		{
			return false;
		}

		TArray<int32, TInlineAllocator<8>> ChangedIndices;
		TArray<int32, TInlineAllocator<8>> AddedIndices;

		DeltaSerializeParams.ReadAddedElements = &AddedIndices;
		DeltaSerializeParams.ReadChangedElements = &ChangedIndices;
		DeltaSerializeParams.ReadNumChanged = Header.NumChanged;
		DeltaSerializeParams.ReadArrayReplicationKey = Header.ArrayReplicationKey;

		if (!Parms.NetSerializeCB->NetDeltaSerializeForFastArray(DeltaSerializeParams))
		{
			return false;
		}

		//---------------
		// Read Changed/New elements
		//---------------

		Helper.template PostReceiveCleanup<>(Header, ChangedIndices, AddedIndices, ArraySerializer.GuidReferencesMap_StructDelta);
	}

	return true;
}


/**
 *	===================== Vector NetSerialization customization. =====================
 *	Provides custom NetSerilization for FVectors.
 *
 *	There are two types of net quantization available:
 *
 *	Fixed Quantization (SerializeFixedVector)
 *		-Fixed number of bits
 *		-Max Value specified as template parameter
 *
 *		Serialized value is scaled based on num bits and max value. Precision is determined by MaxValue and NumBits
 *		(if 2^NumBits is > MaxValue, you will have room for extra precision).
 *
 *		This format is good for things like normals, where the magnitudes are often similar. For example normal values may often
 *		be in the 0.1f - 1.f range. In a packed format, the overhead in serializing num of bits per component would outweigh savings from
 *		serializing very small ( < 0.1f ) values.
 *
 *		It is also good for performance critical sections since you can guarantee byte alignment if that is important.
 *	
 *
 *
 *	Packed Quantization (SerializePackedVector)
 *		-Scaling factor (usually 10, 100, etc)
 *		-Max number of bits per component (this is maximum, not a constant)
 *
 *		The format is <num of bits per component> <N bits for X> <N bits for Y> <N bits for Z>
 *
 *		The advantages to this format are that packed nature. You may support large magnitudes and have as much precision as you want. All while
 *		having small magnitudes take less space.
 *
 *		The trade off is that there is overhead in serializing how many bits are used for each component, and byte alignment is almost always thrown
 *		off.
 *
*/

template<int32 ScaleFactor, int32 MaxBitsPerComponent>
bool WritePackedVector(FVector Value, FArchive& Ar)	// Note Value is intended to not be a reference since we are scaling it before serializing!
{
	check(Ar.IsSaving());

	// Scale vector by quant factor first
	Value *= ScaleFactor;

	// Nan Check
	if( Value.ContainsNaN() )
	{
		logOrEnsureNanError(TEXT("WritePackedVector: Value contains NaN, clearing for safety."));
		FVector	Dummy(0, 0, 0);
		WritePackedVector<ScaleFactor, MaxBitsPerComponent>(Dummy, Ar);
		return false;
	}

	// Some platforms have RoundToInt implementations that essentially reduces the allowed inputs to 2^31.
	const FVector ClampedValue = ClampVector(Value, FVector(-1073741824.0f), FVector(1073741760.0f));
	bool bClamp = ClampedValue != Value;

	// Do basically FVector::SerializeCompressed
	int32 IntX	= FMath::RoundToInt(ClampedValue.X);
	int32 IntY	= FMath::RoundToInt(ClampedValue.Y);
	int32 IntZ	= FMath::RoundToInt(ClampedValue.Z);
			
	uint32 Bits	= FMath::Clamp<uint32>( FMath::CeilLogTwo( 1 + FMath::Max3( FMath::Abs(IntX), FMath::Abs(IntY), FMath::Abs(IntZ) ) ), 1, MaxBitsPerComponent ) - 1;

	// Serialize how many bits each component will have
	Ar.SerializeInt( Bits, MaxBitsPerComponent );

	int32  Bias	= 1<<(Bits+1);
	uint32 Max	= 1<<(Bits+2);
	uint32 DX	= IntX + Bias;
	uint32 DY	= IntY + Bias;
	uint32 DZ	= IntZ + Bias;

	if (DX >= Max) { bClamp=true; DX = static_cast<int32>(DX) > 0 ? Max-1 : 0; }
	if (DY >= Max) { bClamp=true; DY = static_cast<int32>(DY) > 0 ? Max-1 : 0; }
	if (DZ >= Max) { bClamp=true; DZ = static_cast<int32>(DZ) > 0 ? Max-1 : 0; }
	
	Ar.SerializeInt( DX, Max );
	Ar.SerializeInt( DY, Max );
	Ar.SerializeInt( DZ, Max );

	return !bClamp;
}

template<uint32 ScaleFactor, int32 MaxBitsPerComponent>
bool ReadPackedVector(FVector &Value, FArchive& Ar)
{
	uint32 Bits	= 0;

	// Serialize how many bits each component will have
	Ar.SerializeInt( Bits, MaxBitsPerComponent );

	int32  Bias = 1<<(Bits+1);
	uint32 Max	= 1<<(Bits+2);
	uint32 DX	= 0;
	uint32 DY	= 0;
	uint32 DZ	= 0;
	
	Ar.SerializeInt( DX, Max );
	Ar.SerializeInt( DY, Max );
	Ar.SerializeInt( DZ, Max );
	
	
	float fact = (float)ScaleFactor;

	Value.X = (float)(static_cast<int32>(DX)-Bias) / fact;
	Value.Y = (float)(static_cast<int32>(DY)-Bias) / fact;
	Value.Z = (float)(static_cast<int32>(DZ)-Bias) / fact;

	return true;
}

// ScaleFactor is multiplied before send and divided by post receive. A higher ScaleFactor means more precision.
// MaxBitsPerComponent is the maximum number of bits to use per component. This is only a maximum. A header is
// written (size = Log2 (MaxBitsPerComponent)) to indicate how many bits are actually used. 

template<uint32 ScaleFactor, int32 MaxBitsPerComponent>
bool SerializePackedVector(FVector &Vector, FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		return  WritePackedVector<ScaleFactor, MaxBitsPerComponent>(Vector, Ar);
	}

	ReadPackedVector<ScaleFactor, MaxBitsPerComponent>(Vector, Ar);
	return true;
}

// --------------------------------------------------------------

template<int32 MaxValue, int32 NumBits>
struct TFixedCompressedFloatDetails
{
	                                                                // NumBits = 8:
	static constexpr int32 MaxBitValue = (1 << (NumBits - 1)) - 1;  //   0111 1111 - Max abs value we will serialize
	static constexpr int32 Bias = (1 << (NumBits - 1));             //   1000 0000 - Bias to pivot around (in order to support signed values)
	static constexpr int32 SerIntMax = (1 << (NumBits - 0));        // 1 0000 0000 - What we pass into SerializeInt
	static constexpr int32 MaxDelta = (1 << (NumBits - 0)) - 1;     //   1111 1111 - Max delta is

#if !PLATFORM_COMPILER_HAS_IF_CONSTEXPR
	static constexpr float GetInvScale()
	{
		if (MaxValue > MaxBitValue)
		{
			// We have to scale down, scale needs to be a float:
			return (float)MaxValue / (float)MaxBitValue;
		}
		else
		{
			int32 scale = MaxBitValue / MaxValue;
			return 1.f / scale;
		}
	}
#endif
};

template<int32 MaxValue, int32 NumBits>
bool WriteFixedCompressedFloat(const float Value, FArchive& Ar)
{
	using Details = TFixedCompressedFloatDetails<MaxValue, NumBits>;

	bool clamp = false;
	int32 ScaledValue;
	if ( MaxValue > Details::MaxBitValue )
	{
		// We have to scale this down, scale needs to be a float:
		const float scale = (float)Details::MaxBitValue / (float)MaxValue;
		ScaledValue = FMath::TruncToInt(scale * Value);
	}
	else
	{
		// We will scale up to get extra precision. But keep is a whole number preserve whole values
		enum { scale = Details::MaxBitValue / MaxValue };
		ScaledValue = FMath::RoundToInt( scale * Value );
	}

	uint32 Delta = static_cast<uint32>(ScaledValue + Details::Bias);

	if (Delta > Details::MaxDelta)
	{
		clamp = true;
		Delta = static_cast<int32>(Delta) > 0 ? Details::MaxDelta : 0;
	}

	Ar.SerializeInt( Delta, Details::SerIntMax );

	return !clamp;
}

template<int32 MaxValue, int32 NumBits>
bool ReadFixedCompressedFloat(float &Value, FArchive& Ar)
{
	using Details = TFixedCompressedFloatDetails<MaxValue, NumBits>;

	uint32 Delta;
	Ar.SerializeInt(Delta, Details::SerIntMax);
	float UnscaledValue = static_cast<float>( static_cast<int32>(Delta) - Details::Bias );

#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
	if constexpr (MaxValue > Details::MaxBitValue)
	{
		// We have to scale down, scale needs to be a float:
		const float InvScale = MaxValue / (float)Details::MaxBitValue;
		Value = UnscaledValue * InvScale;
	}
	else
	{
		enum { scale = Details::MaxBitValue / MaxValue };
		const float InvScale = 1.f / (float)scale;

		Value = UnscaledValue * InvScale;
	}
#else
	constexpr float InvScale = Details::GetInvScale();
	Value = UnscaledValue * InvScale;
#endif

	return true;
}

// --------------------------------------------------------------
// MaxValue is the max abs value to serialize. If abs value of any vector components exceeds this, the serialized value will be clamped.
// NumBits is the total number of bits to use - this includes the sign bit!
//
// So passing in NumBits = 8, and MaxValue = 2^8, you will scale down to fit into 7 bits so you can leave 1 for the sign bit.
template<int32 MaxValue, int32 NumBits>
bool SerializeFixedVector(FVector &Vector, FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		bool success = true;
		success &= WriteFixedCompressedFloat<MaxValue, NumBits>(Vector.X, Ar);
		success &= WriteFixedCompressedFloat<MaxValue, NumBits>(Vector.Y, Ar);
		success &= WriteFixedCompressedFloat<MaxValue, NumBits>(Vector.Z, Ar);
		return success;
	}

	ReadFixedCompressedFloat<MaxValue, NumBits>(Vector.X, Ar);
	ReadFixedCompressedFloat<MaxValue, NumBits>(Vector.Y, Ar);
	ReadFixedCompressedFloat<MaxValue, NumBits>(Vector.Z, Ar);
	return true;
}

// --------------------------------------------------------------

/**
 *	FVector_NetQuantize
 *
 *	0 decimal place of precision.
 *	Up to 20 bits per component.
 *	Valid range: 2^20 = +/- 1,048,576
 *
 *	Note: this is the historical UE format for vector net serialization
 *
 */
USTRUCT()
struct FVector_NetQuantize : public FVector
{
	GENERATED_USTRUCT_BODY()

	FORCEINLINE FVector_NetQuantize()
	{}

	explicit FORCEINLINE FVector_NetQuantize(EForceInit E)
	: FVector(E)
	{}

	FORCEINLINE FVector_NetQuantize( float InX, float InY, float InZ )
	: FVector(InX, InY, InZ)
	{}

	FORCEINLINE FVector_NetQuantize(const FVector &InVec)
	{
		FVector::operator=(InVec);
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = SerializePackedVector<1, 20>(*this, Ar);
		return true;
	}
};

template<>
struct TStructOpsTypeTraits< FVector_NetQuantize > : public TStructOpsTypeTraitsBase2< FVector_NetQuantize >
{
	enum 
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

/**
 *	FVector_NetQuantize10
 *
 *	1 decimal place of precision.
 *	Up to 24 bits per component.
 *	Valid range: 2^24 / 10 = +/- 1,677,721.6
 *
 */
USTRUCT()
struct FVector_NetQuantize10 : public FVector
{
	GENERATED_USTRUCT_BODY()

	FORCEINLINE FVector_NetQuantize10()
	{}

	explicit FORCEINLINE FVector_NetQuantize10(EForceInit E)
	: FVector(E)
	{}

	FORCEINLINE FVector_NetQuantize10( float InX, float InY, float InZ )
	: FVector(InX, InY, InZ)
	{}

	FORCEINLINE FVector_NetQuantize10(const FVector &InVec)
	{
		FVector::operator=(InVec);
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = SerializePackedVector<10, 24>(*this, Ar);
		return true;
	}
};

template<>
struct TStructOpsTypeTraits< FVector_NetQuantize10 > : public TStructOpsTypeTraitsBase2< FVector_NetQuantize10 >
{
	enum 
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

/**
 *	FVector_NetQuantize100
 *
 *	2 decimal place of precision.
 *	Up to 30 bits per component.
 *	Valid range: 2^30 / 100 = +/- 10,737,418.24
 *
 */
USTRUCT()
struct FVector_NetQuantize100 : public FVector
{
	GENERATED_USTRUCT_BODY()

	FORCEINLINE FVector_NetQuantize100()
	{}

	explicit FORCEINLINE FVector_NetQuantize100(EForceInit E)
	: FVector(E)
	{}

	FORCEINLINE FVector_NetQuantize100( float InX, float InY, float InZ )
	: FVector(InX, InY, InZ)
	{}

	FORCEINLINE FVector_NetQuantize100(const FVector &InVec)
	{
		FVector::operator=(InVec);
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = SerializePackedVector<100, 30>(*this, Ar);
		return true;
	}
};

template<>
struct TStructOpsTypeTraits< FVector_NetQuantize100 > : public TStructOpsTypeTraitsBase2< FVector_NetQuantize100 >
{
	enum 
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

/**
 *	FVector_NetQuantizeNormal
 *
 *	16 bits per component
 *	Valid range: -1..+1 inclusive
 */
USTRUCT()
struct FVector_NetQuantizeNormal : public FVector
{
	GENERATED_USTRUCT_BODY()

	FORCEINLINE FVector_NetQuantizeNormal()
	{}

	explicit FORCEINLINE FVector_NetQuantizeNormal(EForceInit E)
	: FVector(E)
	{}

	FORCEINLINE FVector_NetQuantizeNormal( float InX, float InY, float InZ )
	: FVector(InX, InY, InZ)
	{}

	FORCEINLINE FVector_NetQuantizeNormal(const FVector &InVec)
	{
		FVector::operator=(InVec);
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = SerializeFixedVector<1, 16>(*this, Ar);
		return true;
	}
};

template<>
struct TStructOpsTypeTraits< FVector_NetQuantizeNormal > : public TStructOpsTypeTraitsBase2< FVector_NetQuantizeNormal >
{
	enum 
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

// --------------------------------------------------------------


/**
 *	===================== Safe TArray Serialization ===================== 
 *	
 *	These are helper methods intended to make serializing TArrays safer in custom
 *	::NetSerialize functions. These enforce max limits on array size, so that a malformed
 *	packet is not able to allocate an arbitrary amount of memory (E.g., a hacker serilizes
 *	a packet where a TArray size is of size MAX_int32, causing gigs of memory to be allocated for
 *	the TArray).
 *	
 *	These should only need to be used when you are overriding ::NetSerialize on a UStruct via struct traits.
 *	When using default replication, TArray properties already have this built in security.
 *	
 *	SafeNetSerializeTArray_Default - calls << operator to serialize the items in the array.
 *	SafeNetSerializeTArray_WithNetSerialize - calls NetSerialize to serialize the items in the array.
 *	
 *	When saving, bOutSuccess will be set to false if the passed in array size exceeds to MaxNum template parameter.
 *	
 *	Example:
 *	
 *	FMyStruct {
 *		
 *		TArray<float>						MyFloats;		// We want to call << to serialize floats
 *		TArray<FVector_NetQuantizeNormal>	MyVectors;		// We want to call NetSeriailze on these *		
 *		
 *		bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
 *		{
 *			// Don't do this:
 *			Ar << MyFloats;
 *			Ar << MyVectors;
 *			
 *			// Do this instead:
 *			SafeNetSerializeTArray_Default<31>(Ar, MyFloats);
 *			SafeNetSerializeTArray_WithNetSerialize<31>(Ar, MyVectors, Map);
 *		}
 *	}	
 *	
 */

template<int32 MaxNum, typename T, typename A>
int32 SafeNetSerializeTArray_HeaderOnly(FArchive& Ar, TArray<T, A>& Array, bool& bOutSuccess)
{
	const uint32 NumBits = FMath::CeilLogTwo(MaxNum)+1;
	
	int32 ArrayNum = 0;

	// Clamp number of elements on saving side
	if (Ar.IsSaving())
	{
		ArrayNum = Array.Num();
		if (ArrayNum > MaxNum)
		{
			// Overflow. This is on the saving side, so the calling code is exceeding the limit and needs to be fixed.
			bOutSuccess = false;
			ArrayNum = MaxNum;
		}		
	}

	// Serialize num of elements
	Ar.SerializeBits(&ArrayNum, NumBits);

	// Preallocate new items on loading side
	if (Ar.IsLoading())
	{
		if (ArrayNum > MaxNum)
		{
			// If MaxNum doesn't fully utilize all bits that are needed to send the array size we can receive a larger value.
			bOutSuccess = false;
			ArrayNum = MaxNum;
		}
		Array.Reset();
		Array.AddDefaulted(ArrayNum);
	}

	return ArrayNum;
}

template<int32 MaxNum, typename T, typename A>
bool SafeNetSerializeTArray_Default(FArchive& Ar, TArray<T, A>& Array)
{
	bool bOutSuccess = true;
	int32 ArrayNum = SafeNetSerializeTArray_HeaderOnly<MaxNum, T, A>(Ar, Array, bOutSuccess);

	// Serialize each element in the array with the << operator
	for (int32 idx=0; idx < ArrayNum && Ar.IsError() == false; ++idx)
	{
		Ar << Array[idx];
	}

	// Return
	bOutSuccess &= !Ar.IsError();
	return bOutSuccess;
}

template<int32 MaxNum, typename T, typename A>
bool SafeNetSerializeTArray_WithNetSerialize(FArchive& Ar, TArray<T, A>& Array, class UPackageMap* PackageMap)
{
	bool bOutSuccess = true;
	int32 ArrayNum = SafeNetSerializeTArray_HeaderOnly<MaxNum, T, A>(Ar, Array, bOutSuccess);

	// Serialize each element in the array with the << operator
	for (int32 idx=0; idx < ArrayNum && Ar.IsError() == false; ++idx)
	{
		Array[idx].NetSerialize(Ar, PackageMap, bOutSuccess);
	}

	// Return
	bOutSuccess &= !Ar.IsError();
	return bOutSuccess;
}
