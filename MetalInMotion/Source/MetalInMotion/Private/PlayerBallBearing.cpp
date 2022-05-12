// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerBallBearing.h"
#include "GameFramework/PlayerInput.h"
#include "Components/InputComponent.h"


/**
Create a spring-arm and a camera for this ball bearing on object construction.
*********************************************************************************/

APlayerBallBearing::APlayerBallBearing()
{
	// Create a spring-arm attached to the ball mesh.

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));

	SpringArm->bDoCollisionTest = false;
	SpringArm->SetUsingAbsoluteRotation(true);
	SpringArm->SetRelativeRotation(FRotator(-45.0f, 0.0f, 0.0f));
	SpringArm->TargetArmLength = 1000.0f;
	SpringArm->bEnableCameraLag = false;
	SpringArm->CameraLagSpeed = 5.0f;

	SpringArm->SetupAttachment(BallMesh);

	// Create a camera and attach to the spring-arm.

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));

	Camera->bUsePawnControlRotation = false;

	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

	Magnetized = false;
}


/**
Establish the default pawn input bindings for a player ball bearing.
*********************************************************************************/

static void InitializeDefaultPawnInputBindings()
{
	static bool bindingsAdded = false;

	if (bindingsAdded == false)
	{
		bindingsAdded = true;

		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLongitudinally", EKeys::W, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLongitudinally", EKeys::S, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLongitudinally", EKeys::Up, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLongitudinally", EKeys::Down, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLongitudinally", EKeys::Gamepad_LeftY, 1.f));

		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLaterally", EKeys::A, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLaterally", EKeys::D, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLaterally", EKeys::Left, -1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLaterally", EKeys::Right, 1.f));
		UPlayerInput::AddEngineDefinedAxisMapping(FInputAxisKeyMapping("BallBearing_MoveLaterally", EKeys::Gamepad_LeftX, 1.f));

		UPlayerInput::AddEngineDefinedActionMapping(FInputActionKeyMapping("BallBearing_Jump", EKeys::Enter));
		UPlayerInput::AddEngineDefinedActionMapping(FInputActionKeyMapping("BallBearing_Dash", EKeys::SpaceBar));
	}
}


/**
Called to bind functionality to input.
*********************************************************************************/

void APlayerBallBearing::SetupPlayerInputComponent(UInputComponent* playerInputComponent)
{
	check(playerInputComponent != nullptr);

	Super::SetupPlayerInputComponent(playerInputComponent);

	InitializeDefaultPawnInputBindings();

	playerInputComponent->BindAxis("BallBearing_MoveLongitudinally", this, &APlayerBallBearing::MoveLongitudinally);
	playerInputComponent->BindAxis("BallBearing_MoveLaterally", this, &APlayerBallBearing::MoveLaterally);

	playerInputComponent->BindAction("BallBearing_Jump", EInputEvent::IE_Pressed, this, &APlayerBallBearing::Jump);
	playerInputComponent->BindAction("BallBearing_Dash", EInputEvent::IE_Pressed, this, &APlayerBallBearing::Dash);
}


/**
Have the ball bearing perform a jump.
*********************************************************************************/

void APlayerBallBearing::Jump()
{
	// Only jump if we're in contact with something, normally the ground.

	if (InContact == true)
	{
		// Add the impulse to the ball to perform the jump.

		BallMesh->AddImpulse(FVector(0.0f, 0.0f, JumpForce * 1000.0f));
	}
}


/**
Have the ball bearing perform a dash.
*********************************************************************************/

void APlayerBallBearing::Dash()
{
	// Only dash if we're not dashing already.

	if (DashTimer == 0.0f)
	{
		// Only dash if we have an existing velocity vector to dash towards.

		FVector Velocity = BallMesh->GetPhysicsLinearVelocity();

		if (Velocity.Size() > 1.0f)
		{
			Velocity.Normalize();
			Velocity *= DashForce * 1000.0f;

			// Add the impulse to the ball to perform the dash.

			BallMesh->AddImpulse(Velocity);

			// Set the length of time that we're to dash for.

			DashTimer = 1.5f;
		}
	}
}


/**
Control the movement of the ball bearing, called every frame.
*********************************************************************************/

void APlayerBallBearing::Tick(float deltaSeconds)
{
	Super::Tick(deltaSeconds);

	FVector Velocity = BallMesh->GetPhysicsLinearVelocity();
	float VelZ = Velocity.Z;

	Velocity.Z = 0.0f;

	if (Velocity.Size() > MaximumSpeed * 100.0f)
	{
		Velocity.Normalize();
		Velocity *= MaximumSpeed * 100.0f;
		Velocity.Z = VelZ;

		float BrakingRatio = FMath::Pow(1.0f - FMath::Min(DashTimer, 1.0f), 2.0f);

		FVector MergedVelocity = FMath::Lerp(BallMesh->GetPhysicsLinearVelocity(), Velocity, BrakingRatio);

		BallMesh->SetPhysicsLinearVelocity(MergedVelocity);
	}
	else
	{
		BallMesh->AddForce(FVector(InputLongitude, InputLatitude, 0.0f) * ControllerForce * BallMesh->GetMass());
	}

	if (DashTimer > 0.0f)
	{
		DashTimer = FMath::Max(0.0f, DashTimer - deltaSeconds);
	}
}