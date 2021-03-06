// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"

class SButton;
class UGroomCreateBindingOptions;

class SGroomCreateBindingOptionsWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGroomCreateBindingOptionsWindow)
		: _BindingOptions(nullptr)
		, _WidgetWindow()
		, _FullPath()
		, _ButtonLabel()
	{}

	SLATE_ARGUMENT(UGroomCreateBindingOptions*, BindingOptions)
	SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_ARGUMENT(FText, FullPath)
	SLATE_ARGUMENT(FText, ButtonLabel)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }

	static TSharedPtr<SGroomCreateBindingOptionsWindow> DisplayCreateBindingOptions(UGroomCreateBindingOptions* BindingOptions);

	FReply OnCreateBinding()
	{
		bShouldCreate = true;
		if (WidgetWindow.IsValid())
		{
			WidgetWindow.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	FReply OnCancel()
	{
		bShouldCreate = false;
		if (WidgetWindow.IsValid())
		{
			WidgetWindow.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if(InKeyEvent.GetKey() == EKeys::Escape)
		{
			return OnCancel();
		}

		return FReply::Unhandled();
	}

	bool ShouldCreate() const
	{
		return bShouldCreate;
	}

	SGroomCreateBindingOptionsWindow() 
		: BindingOptions(nullptr)
		, bShouldCreate(false)
	{}

private:

	bool CanCreateBinding() const;

private:
	UGroomCreateBindingOptions* BindingOptions;
	TSharedPtr<class IDetailsView> DetailsView;
	TWeakPtr<SWindow> WidgetWindow;
	TSharedPtr<SButton> ImportButton;
	bool bShouldCreate;
};
