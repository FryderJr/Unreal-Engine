// Copyright Epic Games, Inc. All Rights Reserved.

#include "DestructibleMeshDetails.h"
#include "Engine/SkeletalMesh.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DestructibleMesh.h"

#define LOCTEXT_NAMESPACE "DestructibleMeshDetails"

TSharedRef<IDetailCustomization> FDestructibleMeshDetails::MakeInstance()
{
	return MakeShareable(new FDestructibleMeshDetails);
}

void AddStructToDetails(FName CategoryName, FName PropertyName, IDetailLayoutBuilder& DetailBuilder, bool bInline = true, bool bAdvanced = false)
{
	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(CategoryName, FText::GetEmpty(), ECategoryPriority::Important);
	TSharedPtr<IPropertyHandle> Params = DetailBuilder.GetProperty(PropertyName);
	if (Params.IsValid())
	{
		EPropertyLocation::Type PropertyLocation = bAdvanced ? EPropertyLocation::Advanced : EPropertyLocation::Default;
		if (bInline)
		{
			uint32 NumChildren = 0;
			Params->GetNumChildren(NumChildren);

			// add all collision properties
			for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
			{
				TSharedPtr<IPropertyHandle> ChildProperty = Params->GetChildHandle(ChildIndex);
				Category.AddProperty(ChildProperty, PropertyLocation);
			}
		}
		else
		{
			Category.AddProperty(Params, PropertyLocation);
		}
	}
}

void FDestructibleMeshDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		//rest of customization is just moving stuff out of DefaultDestructibleParameters so it's nicer to view
		TSharedPtr<IPropertyHandle> DefaultParams = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDestructibleMesh, DefaultDestructibleParameters));
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (DefaultParams.IsValid() == false)
		{
			return;
		}

		AddStructToDetails("Damage", "DefaultDestructibleParameters.DamageParameters", DetailBuilder);
		AddStructToDetails("Damage", "DefaultDestructibleParameters.AdvancedParameters", DetailBuilder, true, true);
		AddStructToDetails("Debris", "DefaultDestructibleParameters.DebrisParameters", DetailBuilder);
		AddStructToDetails("Flags", "DefaultDestructibleParameters.Flags", DetailBuilder);
		AddStructToDetails("HierarchyDepth", "DefaultDestructibleParameters.SpecialHierarchyDepths", DetailBuilder);
		AddStructToDetails("HierarchyDepth", "DefaultDestructibleParameters.DepthParameters", DetailBuilder, false, true);

		// There are some properties we inherit that aren't supported on destructibles, hide them from the details panel.
		HideUnsupportedProperties(DetailBuilder);

		//hide the default params as we've taken everything out of it
		DetailBuilder.HideProperty(DefaultParams);
}

void FDestructibleMeshDetails::HideUnsupportedProperties(IDetailLayoutBuilder &DetailBuilder)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Body setups are not available on destructible meshes as we set up the bodies through APEX
	TSharedPtr<IPropertyHandle> BodySetupHandle = DetailBuilder.GetProperty(USkeletalMesh::GetBodySetupMemberName());
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if(BodySetupHandle.IsValid())
	{
		BodySetupHandle->MarkHiddenByCustomization();
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Capsule shadows only supported on skeletal meshes
	TSharedPtr<IPropertyHandle> ShadowPhysicsAssetHandle = DetailBuilder.GetProperty(USkeletalMesh::GetShadowPhysicsAssetMemberName(), USkeletalMesh::StaticClass());
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if(ShadowPhysicsAssetHandle.IsValid())
	{
		ShadowPhysicsAssetHandle->MarkHiddenByCustomization();
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Post processing graphs only supported on skeletal meshes
	TSharedPtr<IPropertyHandle> PostProcessBlueprintHandle = DetailBuilder.GetProperty(USkeletalMesh::GetPostProcessAnimBlueprintMemberName(), USkeletalMesh::StaticClass());
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if(PostProcessBlueprintHandle.IsValid())
	{
		PostProcessBlueprintHandle->MarkHiddenByCustomization();
	}
}

#undef LOCTEXT_NAMESPACE
