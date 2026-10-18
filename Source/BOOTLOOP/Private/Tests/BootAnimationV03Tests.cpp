#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Player/BootAvatarComponent.h"
#include "Player/BootFeedbackComponent.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"

namespace
{
    struct FAnimationWorld
    {
        UWorld* World;
        ACharacter* Character;
        UBootAvatarComponent* Avatar;
        UBootFeedbackComponent* Feedback;
        explicit FAnimationWorld(bool bAudio = false)
        {
            UWorld::InitializationValues Init;
            Init.AllowAudioPlayback(bAudio).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            Character = World->SpawnActor<ACharacter>();
            Avatar = NewObject<UBootAvatarComponent>(Character);
            Character->AddInstanceComponent(Avatar); Avatar->RegisterComponent();
            Feedback = NewObject<UBootFeedbackComponent>(Character);
            Character->AddInstanceComponent(Feedback); Feedback->RegisterComponent();
        }
        ~FAnimationWorld()
        {
            World->EndPlay(EEndPlayReason::Quit);
            GEngine->DestroyWorldContext(World); World->DestroyWorld(false);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03ContactTest, "Bootloop.V03.Animation.ContactCadenceAndMovingBase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV03ContactTest::RunTest(const FString&)
{
    for (int32 Fps : {30, 60, 120})
    {
        FAnimationWorld Scene;
        TArray<int32> Feet;
        TArray<float> ContactTimes;
        float Time = 0.f;
        Scene.Avatar->OnFootContact.AddLambda([&](int32 Foot, FVector Position)
        {
            Feet.Add(Foot); ContactTimes.Add(Time);
            TestFalse(TEXT("Contact position finite"), Position.ContainsNaN());
        });
        FBootPoseInput Input;
        Input.WorldPlanarSpeed = 1200.f; // Riding without walking must remain silent.
        for (int32 I = 0; I < Fps * 2; ++I)
        {
            Scene.Character->AddActorWorldOffset(FVector(1200.f / Fps, 0, 0));
            Scene.Avatar->UpdatePose(1.f / Fps, Input);
        }
        TestEqual(TEXT("Moving support alone produces no footsteps"), Feet.Num(), 0);
        Scene.Avatar->SetBodyVisible(false);
        Input.LocalVelocity = FVector(1000, 0, 0);
        Input.WorldPlanarSpeed = 1000.f;
        // 1000cm/s => two cycles/s; ten cycles contain exactly twenty contacts.
        for (int32 I = 0; I < Fps * 5; ++I)
        {
            Time += 1.f / Fps;
            Scene.Character->AddActorWorldOffset(FVector(1000.f / Fps, 0, 0));
            Scene.Avatar->UpdatePose(1.f / Fps, Input);
        }
        TestEqual(FString::Printf(TEXT("Two alternating contacts per cycle, Body Off, %dFPS"), Fps), Feet.Num(), 20);
        for (int32 I = 1; I < Feet.Num(); ++I)
        {
            TestTrue(TEXT("Feet alternate"), Feet[I] != Feet[I - 1]);
            TestTrue(TEXT("Contact cadence <=4.5/s"), ContactTimes[I] - ContactTimes[I - 1] >= (1.f / 4.5f - 1.f / Fps));
        }
        const int32 BeforeTeleport = Feet.Num();
        Scene.Character->AddActorWorldOffset(FVector(10000, 0, 0));
        Scene.Avatar->UpdatePose(1.f / Fps, Input);
        TestEqual(TEXT("Teleport does not produce contact"), Feet.Num(), BeforeTeleport);
        Scene.Avatar->ResetPose();
        Input.LocalVelocity = FVector(1800, 0, 0);
        Feet.Reset(); ContactTimes.Reset(); Time = 0;
        for (int32 I = 0; I < Fps * 10; ++I)
        {
            Time += 1.f / Fps;
            Scene.Avatar->UpdatePose(1.f / Fps, Input);
        }
        TestTrue(TEXT("Maximum speed contact rate bounded"), Feet.Num() >= 40 && Feet.Num() <= 44);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03LimbTest, "Bootloop.V03.Animation.TransitionsAndLimbLengths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV03LimbTest::RunTest(const FString&)
{
    FAnimationWorld Scene;
    FBootPoseInput Input;
    Input.LocalVelocity = FVector(900, 350, 0);
    Input.LocalAcceleration = FVector(6000, -2000, 0);
    Input.AimPitch = -65.f; Input.TurnRateDegrees = 180.f;
    float LargestError = 0.f;
    int32 Segments = 0;
    for (int32 Frame = 0; Frame < 600; ++Frame)
    {
        const int32 Mode = (Frame / 60) % 5;
        Input.bGrounded = Mode != 1 && Mode != 3;
        Input.bCrouched = Mode == 2; Input.bSliding = Mode == 2; Input.bWallRunning = Mode == 3;
        Input.WallSide = Frame < 300 ? -1.f : 1.f;
        Input.VerticalVelocity = Mode == 1 ? 500.f - float(Frame % 60) * 20.f : 0.f;
        if (Frame % 90 == 0) Scene.Avatar->TriggerSnap();
        Scene.Avatar->UpdatePose(1.f / 60.f, Input);
        TArray<UStaticMeshComponent*> Parts;
        Scene.Character->GetComponents(Parts);
        for (UStaticMeshComponent* Part : Parts)
        {
            TestTrue(TEXT("Every joint transform stays finite"), Part->GetRelativeTransform().IsValid());
            const FString Name = Part->GetName();
            float Length = Name.Contains(TEXT("UpperArm")) ? 28.f : Name.Contains(TEXT("Forearm")) ? 30.f :
                Name.Contains(TEXT("Thigh")) ? 31.f : Name.Contains(TEXT("Shin")) ? 30.f : 0.f;
            if (Length > 0.f)
            {
                ++Segments;
                LargestError = FMath::Max(LargestError, FMath::Abs(float(Part->GetRelativeScale3D().Z) - 1.f) * Length);
            }
        }
    }
    TestTrue(TEXT("Native limbs were created"), Segments >= 8);
    TestTrue(FString::Printf(TEXT("Fixed limb length error <=1cm (%.3fcm)"), LargestError), LargestError <= 1.f);
    return true;
}

namespace
{
    // Clip the convex OBB at the camera near plane before dividing by depth.
    // The clipped vertices consist of surviving corners and intersections on its 12 edges.
    bool ProjectClippedObb(const FBox& Box, const FTransform& MeshFrame, const FTransform& CameraFrame,
        double TanH, double TanV, FBox2D& Bounds)
    {
        constexpr double NearX = 1.;
        Bounds = FBox2D(ForceInit);
        FVector Corners[8];
        bool bAny = false;
        auto Add = [&](const FVector& P)
        {
            Bounds += FVector2D(.5 + P.Y / (2. * P.X * TanH), .5 - P.Z / (2. * P.X * TanV));
            bAny = true;
        };
        for (int32 Corner = 0; Corner < 8; ++Corner)
        {
            const FVector Local(Corner & 1 ? Box.Max.X : Box.Min.X,
                Corner & 2 ? Box.Max.Y : Box.Min.Y, Corner & 4 ? Box.Max.Z : Box.Min.Z);
            Corners[Corner] = CameraFrame.InverseTransformPosition(MeshFrame.TransformPosition(Local));
            if (Corners[Corner].X >= NearX) Add(Corners[Corner]);
        }
        for (int32 Corner = 0; Corner < 8; ++Corner)
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                const int32 Other = Corner ^ (1 << Axis);
                if (Other <= Corner) continue;
                const FVector A = Corners[Corner], B = Corners[Other];
                if ((A.X < NearX) == (B.X < NearX)) continue;
                const double Alpha = (NearX - A.X) / (B.X - A.X);
                FVector Intersection = FMath::Lerp(A, B, Alpha);
                Intersection.X = NearX;
                Add(Intersection);
            }
        return bAny;
    }
    bool ObstructsLanding(const FBox2D& Bounds)
    {
        return Bounds.Max.X > .40 && Bounds.Min.X < .60 && Bounds.Max.Y > .32 && Bounds.Min.Y < .62;
    }
}

// Natural v0.4 hands may leave the forward view; the central landing corridor stays clear.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03ForwardHandsTest, "Bootloop.V03.Animation.ForwardHandsProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV03ForwardHandsTest::RunTest(const FString&)
{
    const double ControlTanH = FMath::Tan(FMath::DegreesToRadians(47.5));
    const double ControlTanV = ControlTanH / (16. / 9.);
    const FBox UnitBox(FVector(-1,-1,-1), FVector(1,1,1));
    const FBox NearBox(FVector(-5,-1,-1), FVector(5,1,1));
    FBox2D ControlBounds(ForceInit);
    auto Control = [&](const FBox& Box, FVector Position)
    {
        return ProjectClippedObb(Box, FTransform(FQuat::Identity,Position), FTransform::Identity,
            ControlTanH, ControlTanV, ControlBounds);
    };
    TestFalse(TEXT("Projection control: wholly behind camera has no projected vertices"), Control(UnitBox,FVector(-30,0,0)));
    TestTrue(TEXT("Projection control: fully below camera projects"), Control(UnitBox,FVector(30,0,-100)));
    TestFalse(TEXT("Projection control: fully below camera cannot obstruct centre"), ObstructsLanding(ControlBounds));
    TestTrue(TEXT("Projection control: below OBB crossing near plane is retained"), Control(NearBox,FVector(0,0,-100)));
    TestFalse(TEXT("Projection control: near-plane crossing below camera cannot obstruct centre"), ObstructsLanding(ControlBounds));
    TestTrue(TEXT("Projection control: true central OBB projects"), Control(UnitBox,FVector(30,0,0)));
    TestTrue(TEXT("Projection control: true central OBB obstructs centre"), ObstructsLanding(ControlBounds));
    TestTrue(TEXT("Projection control: central OBB crossing near plane projects clipped edges"), Control(NearBox,FVector::ZeroVector));
    TestTrue(TEXT("Projection control: near central obstruction cannot disappear"), ObstructsLanding(ControlBounds));

    for (int32 Fps : {30, 60, 120})
    {
        FAnimationWorld Scene;
        auto* Capsule = Scene.Character->GetCapsuleComponent();
        Capsule->SetCapsuleHalfHeight(96.f);
        auto* Camera = NewObject<UCameraComponent>(Scene.Character);
        Scene.Character->AddInstanceComponent(Camera);
        Camera->SetupAttachment(Capsule);
        Camera->SetRelativeLocation(FVector(0, 0, 64));
        Camera->SetFieldOfView(95.f); Camera->SetAspectRatio(16.f / 9.f);
        Camera->RegisterComponent();
        TArray<UStaticMeshComponent*> Parts, Hands;
        Scene.Character->GetComponents(Parts);
        for (UStaticMeshComponent* Part : Parts)
            if (Part->GetName().StartsWith(TEXT("ShellPalm")) || Part->GetName().StartsWith(TEXT("ShellWrist")))
                Hands.Add(Part);
        TestEqual(TEXT("Both native palms and CRT wrists are present"), Hands.Num(), 4);
        for (int32 Mode = 0; Mode < 6; ++Mode)
        {
            Scene.Avatar->ResetPose();
            const bool bSlide = Mode == 3;
            Capsule->SetCapsuleHalfHeight(bSlide ? 44.f : 96.f);
            const float Eye = bSlide ? 28.f : 64.f;
            Camera->SetRelativeLocation(FVector(0, 0, Eye));
            FBootPoseInput Input;
            Input.CapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
            Input.LocalVelocity = Mode == 0 ? FVector::ZeroVector : FVector(900, 0, 0);
            Input.bGrounded = Mode != 2 && Mode < 4;
            Input.bSliding = bSlide; Input.bCrouched = bSlide;
            Input.bWallRunning = Mode >= 4; Input.WallSide = Mode == 4 ? -1.f : 1.f;
            Input.VerticalVelocity = Mode == 2 ? 300.f : 0.f;
            // Allow authored blends to settle, then inspect two seconds including every jog phase.
            for (int32 I = 0; I < Fps / 2; ++I) Scene.Avatar->UpdatePose(1.f / Fps, Input);
            int32 CroppedFrames = 0, CenterFrames = 0;
            float LeastVisible = 1.f;
            FString LeastVisibleBounds;
            for (int32 I = 0; I < Fps * 2; ++I)
            {
                Scene.Avatar->UpdatePose(1.f / Fps, Input);
                const FTransform CameraFrame = Camera->GetComponentTransform();
                const double TanH = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView * .5f));
                const double TanV = TanH / Camera->AspectRatio;
                for (UStaticMeshComponent* Part : Hands)
                {
                    const FBox Box = Part->GetStaticMesh()->GetBoundingBox();
                    FBox2D ScreenBounds(ForceInit);
                    if (!ProjectClippedObb(Box, Part->GetComponentTransform(), CameraFrame, TanH, TanV, ScreenBounds))
                    {
                        LeastVisible = 0.f; // Entirely behind the near plane; no obstruction.
                        continue;
                    }
                    const FVector2D Center = ScreenBounds.GetCenter();
                    const FVector2D Size = ScreenBounds.GetSize();
                    const double VisibleWidth = FMath::Max(0., FMath::Min(1., ScreenBounds.Max.X) - FMath::Max(0., ScreenBounds.Min.X));
                    const double VisibleHeight = FMath::Max(0., FMath::Min(1., ScreenBounds.Max.Y) - FMath::Max(0., ScreenBounds.Min.Y));
                    const float VisibleFraction = float(VisibleWidth * VisibleHeight / FMath::Max(.00001, Size.X * Size.Y));
                    if (VisibleFraction < LeastVisible)
                    {
                        LeastVisible = VisibleFraction;
                        LeastVisibleBounds = FString::Printf(TEXT("%s world%s uv%s bounds%s..%s"),
                            *Part->GetName(), *Part->GetComponentLocation().ToString(), *Center.ToString(),
                            *ScreenBounds.Min.ToString(), *ScreenBounds.Max.ToString());
                    }
                    if (Center.X < .04 || Center.X > .96 || Center.Y < .60 || Center.Y > .96 || VisibleFraction < .65f)
                        ++CroppedFrames;
                    // Keep a central 20%-wide view clear through the landing approach/crosshair.
                    if (ObstructsLanding(ScreenBounds))
                    {
                        if (CenterFrames == 0)
                            AddInfo(FString::Printf(TEXT("Actual clipped obstruction %s mode%d %dFPS frame%d bounds%s..%s"),
                                *Part->GetName(), Mode, Fps, I, *ScreenBounds.Min.ToString(), *ScreenBounds.Max.ToString()));
                        ++CenterFrames;
                    }
                }
            }
            // Visibility fractions remain diagnostics; forcing both palms into view contradicts natural arm poses.
            TestEqual(FString::Printf(TEXT("Central landing view clear, mode%d %dFPS"), Mode, Fps), CenterFrames, 0);
            AddInfo(FString::Printf(TEXT("Forward projection mode%d %dFPS least%.3f %s"),
                Mode, Fps, LeastVisible, *LeastVisibleBounds));
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03WristChargeTest, "Bootloop.V03.Animation.WristAirRedirectCue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV03WristChargeTest::RunTest(const FString&)
{
    FAnimationWorld Scene;
    FBootPoseInput Input;
    TestTrue(TEXT("Fresh pose has an available redirect charge"), Input.bAirRedirectReady);
    TArray<UStaticMeshComponent*> Parts;
    Scene.Character->GetComponents(Parts);
    UStaticMeshComponent* Cue = nullptr;
    int32 CueCount = 0;
    for (UStaticMeshComponent* Part : Parts)
        if (Part->GetName().StartsWith(TEXT("ShellRedirectCue"))) { Cue = Part; ++CueCount; }
    TestEqual(TEXT("One native wrist charge cue is created"), CueCount, 1);
    if (!TestNotNull(TEXT("Actual wrist cue exists on the first pose"), Cue)) return false;
    auto* Material = Cast<UMaterialInstanceDynamic>(Cue->GetMaterial(0));
    if (!TestNotNull(TEXT("Cue uses an actual dynamic native unlit material"), Material)) return false;
    TestTrue(TEXT("Cue belongs to the left CRT wrist"), Cue->GetAttachParent() && Cue->GetAttachParent()->GetName() == TEXT("ShellWrist0"));
    TestEqual(TEXT("Cue cannot affect collision"), Cue->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Cue cannot trigger overlaps"), Cue->GetGenerateOverlapEvents());
    TestTrue(TEXT("Initial ready cue is visible"), Cue->IsVisible());
    const FLinearColor ReadyColor = Material->K2_GetVectorParameterValue(TEXT("FlatColor"));
    const FVector ReadySize = Cue->GetRelativeScale3D();
    TestTrue(TEXT("Ready cue is a mint status bar"), ReadyColor.G > .5f && ReadySize.X > ReadySize.Y * 3.f);
    Input.bAirRedirectReady = false;
    Scene.Avatar->UpdatePose(1.f / 60.f, Input);
    const FLinearColor SpentColor = Material->K2_GetVectorParameterValue(TEXT("FlatColor"));
    const FVector SpentSize = Cue->GetRelativeScale3D();
    TestTrue(TEXT("Spending charge changes the actual material to a dim state"), ReadyColor.G > SpentColor.G + .4f);
    TestTrue(TEXT("Spent cue has a shorter, distinct shape"), ReadySize.X > SpentSize.X * 2.f && FMath::IsNearlyEqual(SpentSize.X, SpentSize.Y, .001));
    Scene.Avatar->SetBodyVisible(false);
    Input.bAirRedirectReady = true;
    Scene.Avatar->UpdatePose(1.f / 60.f, Input);
    TestFalse(TEXT("Body Off hides the cue even when recharge updates its state"), Cue->IsVisible());
    TestTrue(TEXT("Ground recharge restores mint state"), Material->K2_GetVectorParameterValue(TEXT("FlatColor")).Equals(ReadyColor, .001f));
    Input.bAirRedirectReady = false;
    Scene.Avatar->UpdatePose(1.f / 60.f, Input);
    Scene.Avatar->ResetPose();
    TestTrue(TEXT("Reset restores ready appearance"), Material->K2_GetVectorParameterValue(TEXT("FlatColor")).Equals(ReadyColor, .001f));
    TestTrue(TEXT("Reset restores ready bar shape"), Cue->GetRelativeScale3D().Equals(ReadySize, .001));
    TestFalse(TEXT("Reset preserves Body Off preference"), Cue->IsVisible());
    Scene.Avatar->SetBodyVisible(true);
    TestTrue(TEXT("Body On restores ready cue visibility"), Cue->IsVisible());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03FeedbackTest, "Bootloop.V03.Animation.FeedbackPauseReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV03FeedbackTest::RunTest(const FString&)
{
    FAnimationWorld Scene;
    TestFalse(TEXT("Idle feedback has no tick"), Scene.Feedback->IsComponentTickEnabled());
    Scene.Feedback->ApplyPreferences(1.f, 0.f, true);
    for (int32 I = 0; I < 1000; ++I)
        Scene.Feedback->Notify(EBootFeedbackEvent::Snap, FVector(0, 0, 10), FVector::UpVector);
    const int32 Count = Scene.Feedback->GetActiveParticleCount();
    TestTrue(TEXT("Cosmetic pool active and bounded by64"), Count > 0 && Count <= 64);
    Scene.Feedback->TickComponent(0.f, LEVELTICK_All, nullptr);
    TestEqual(TEXT("Zero elapsed time preserves particles"), Scene.Feedback->GetActiveParticleCount(), Count);
    auto* Pauser = Scene.World->SpawnActor<APlayerState>();
    Scene.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    TestTrue(TEXT("World uses actual engine pause state"), Scene.World->IsPaused());
    Scene.Feedback->TickComponent(1.f, LEVELTICK_All, nullptr);
    TestEqual(TEXT("Paused elapsed time preserves particles"), Scene.Feedback->GetActiveParticleCount(), Count);
    Scene.Feedback->ResetFeedback();
    Scene.Feedback->Notify(EBootFeedbackEvent::Snap, FVector::ZeroVector, FVector::UpVector);
    TestEqual(TEXT("Paused Notify cannot start effects"), Scene.Feedback->GetActiveParticleCount(), 0);
    Scene.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    Scene.Feedback->Notify(EBootFeedbackEvent::Snap, FVector::ZeroVector, FVector::UpVector);
    TArray<UStaticMeshComponent*> Parts;
    Scene.Character->GetComponents(Parts);
    for (UStaticMeshComponent* Part : Parts)
        TestEqual(TEXT("Avatar and particles never own collision"), Part->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    for (int32 I = 0; I < 120; ++I)
        Scene.Feedback->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
    TestEqual(TEXT("Resume expires cosmetic particles"), Scene.Feedback->GetActiveParticleCount(), 0);
    TestFalse(TEXT("Expired pool stops ticking"), Scene.Feedback->IsComponentTickEnabled());
    for (int32 I = 0; I < 10; ++I)
    {
        Scene.Feedback->Notify(EBootFeedbackEvent::Land, FVector::ZeroVector, FVector::UpVector);
        Scene.Feedback->ResetFeedback(); Scene.Avatar->ResetPose();
        TestEqual(TEXT("Retry clears particles"), Scene.Feedback->GetActiveParticleCount(), 0);
        TestFalse(TEXT("Retry stops pool tick"), Scene.Feedback->IsComponentTickEnabled());
    }
    Scene.Feedback->ApplyPreferences(0.f, 0.f, false);
    Scene.Feedback->Notify(EBootFeedbackEvent::Checkpoint, FVector::ZeroVector, FVector::UpVector);
    TestEqual(TEXT("Effects preference suppresses cosmetic pool"), Scene.Feedback->GetActiveParticleCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03FeedbackOriginTest, "Bootloop.V03.Animation.FeedbackCapsuleContactOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV03FeedbackOriginTest::RunTest(const FString&)
{
    FAnimationWorld Scene;
    Scene.Feedback->ApplyPreferences(1.f, 0.f, true);
    UCapsuleComponent* Capsule = Scene.Character->GetCapsuleComponent();
    for (int32 Case = 0; Case < 3; ++Case)
    {
        Scene.Feedback->ResetFeedback();
        Capsule->SetCapsuleHalfHeight(Case == 0 ? 96.f : 44.f);
        Capsule->SetWorldScale3D(FVector(Case == 2 ? 1.5f : 1.f));
        Capsule->SetWorldLocation(FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight()));
        FBootPoseInput Input;
        Input.LocalVelocity = FVector(900, 0, 0); Input.bSliding = true; Input.bCrouched = Case != 0;
        Scene.Feedback->UpdateMovement(.1f, Input);
        TestTrue(TEXT("Sliding creates a derived contact trail"), Scene.Feedback->GetActiveParticleCount() > 0);
        TArray<UStaticMeshComponent*> Parts;
        Scene.Character->GetComponents(Parts);
        for (UStaticMeshComponent* Part : Parts)
            if (Part->GetName().StartsWith(TEXT("FeedbackChip")) && Part->IsVisible())
                TestTrue(FString::Printf(TEXT("Standing/crouched/scaled trail starts 3cm above floor, case%d z=%.2f"),
                    Case, Part->GetComponentLocation().Z), FMath::IsNearlyEqual(Part->GetComponentLocation().Z, 3., .1));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootV03FeedbackAudioTest, "Bootloop.V03.Animation.FeedbackGameplayAudioPause",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootV03FeedbackAudioTest::RunTest(const FString&)
{
    FAnimationWorld Scene(true);
    const FAudioDeviceHandle Audio = Scene.World->GetAudioDevice();
    if (!GEngine->UseSound() || !Audio.IsValid())
    {
        AddInfo(TEXT("Audio-device coverage unavailable in this process; rerun with sound enabled. This run does not establish playback pause."));
        return true;
    }
    Scene.Feedback->ApplyPreferences(0.f, 1.f, true);
    USoundBase* Snap = LoadObject<USoundBase>(nullptr, TEXT("/Game/Assets/AudioV02/S_Snap.S_Snap"));
    TestNotNull(TEXT("Native gameplay SNAP sound exists"), Snap);
    const uint32 WorldId = Scene.World->GetUniqueID();
    auto Inspect = [&]()
    {
        int32 Matching = 0, UiSounds = 0;
        FAudioThread::RunCommandOnAudioThread([&]()
        {
            for (const FActiveSound* Sound : Audio->GetActiveSounds())
                if (Sound->GetWorldID() == WorldId && Sound->GetSound() == Snap)
                { ++Matching; if (Sound->bIsUISound) ++UiSounds; }
        });
        FAudioCommandFence Fence; Fence.BeginFence(); Fence.Wait();
        return FIntPoint(Matching, UiSounds);
    };
    Scene.Feedback->Notify(EBootFeedbackEvent::Snap, FVector::ZeroVector, FVector::UpVector);
    const FIntPoint Active = Inspect();
    TestTrue(TEXT("Notify submits actual gameplay sound"), Active.X > 0);
    TestEqual(TEXT("Gameplay one-shots follow gameplay pause rather than UI playback"), Active.Y, 0);
    auto* Pauser = Scene.World->SpawnActor<APlayerState>();
    Scene.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    Scene.Feedback->Notify(EBootFeedbackEvent::Snap, FVector::ZeroVector, FVector::UpVector);
    TestEqual(TEXT("Paused Notify submits no new active sound"), Inspect().X, Active.X);
    Scene.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    return true;
}
#endif

