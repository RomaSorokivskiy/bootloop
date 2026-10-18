#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "World/Generation/BootEndlessTrack.h"
#include "Tutorial/BootFirstRunTrack.h"
#include "World/Presentation/BootDesktopBackdrop.h"
#include "Music/BootMusicTypes.h"
#include "Movement/BootTraversalEnvelope.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

namespace
{
struct FPresentationIntegrationWorld
{
    UWorld* World;
    AActor* Follow;
    FPresentationIntegrationWorld()
    {
        UWorld::InitializationValues Init;
        Init.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
        Follow=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Follow);
        Follow->AddInstanceComponent(Root);Follow->SetRootComponent(Root);Root->RegisterComponent();
    }
    ~FPresentationIntegrationWorld()
    {
        World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
    }
    TArray<ABootDesktopBackdrop*> Districts() const
    {
        TArray<ABootDesktopBackdrop*> Result;
        for(TActorIterator<ABootDesktopBackdrop> It(World);It;++It)if(IsValid(*It))Result.Add(*It);
        return Result;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootPresentationIntegrationV04,
    "Bootloop.V04.Feedback.RunPresentationOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootPresentationIntegrationV04::RunTest(const FString&)
{
    for(bool bFirstRun:{false,true})
    {
        FPresentationIntegrationWorld S;
        ABootTrack* Track=bFirstRun?static_cast<ABootTrack*>(S.World->SpawnActor<ABootFirstRunTrack>()):
            static_cast<ABootTrack*>(S.World->SpawnActor<ABootEndlessTrack>());
        TestEqual(TEXT("Presentation opt-in does not affect legacy base construction"),S.Districts().Num(),0);
        Track->InitializeRunPresentation(9504,S.Follow);
        const auto Initial=S.Districts();
        if(!TestEqual(bFirstRun?TEXT("FirstRun owns one city"):TEXT("MusicRun owns one city"),Initial.Num(),1))continue;
        auto* City=Initial[0];const int32 PartCount=City->GetDecorativePartCount();
        TestTrue(TEXT("Actual city exists beyond mint sky"),PartCount>1000);
        TestTrue(TEXT("Track owns city actor"),City->GetOwner()==Track);
        FBootMusicContext Context;Context.TrackHash=TEXT("run-presentation-fixture");
        const auto Envelope=FBootTraversalPolicy::FromTempo(130.,1);
        for(int32 Retry=0;Retry<20;++Retry)
        {
            Track->InitializeRunPresentation(9504,S.Follow);
            Context.TrackEpoch=Retry;Context.Section=Retry%6;Track->ApplyRunPresentation(Context,Envelope);
            const auto Current=S.Districts();TestEqual(TEXT("Retry never grows city actor count"),Current.Num(),1);
            if(Current.Num()==1)TestTrue(TEXT("Retry reuses actual actor"),Current[0]==City);
            TestEqual(TEXT("Retry never grows city geometry"),City->GetDecorativePartCount(),PartCount);
        }
        const FVector P(260000,-180000,62000);S.Follow->SetActorLocation(P);City->Tick(1.f/60.f);
        TestTrue(TEXT("Owned city follows full XYZ"),City->GetActorLocation().Equals(P,.01));
        TArray<UPrimitiveComponent*> Geometry;City->GetComponents(Geometry);
        for(auto* Part:Geometry)
        {
            TestEqual(TEXT("City cannot change real route collision"),Part->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            TestFalse(TEXT("City cannot trigger hazards"),Part->GetGenerateOverlapEvents());
        }
        Track->Destroy();
        TestEqual(TEXT("Destroying track destroys owned city"),S.Districts().Num(),0);
    }
    return true;
}
#endif
