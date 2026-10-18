#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
#include "World/Presentation/BootRunEffectsComponent.h"
#include "World/Presentation/BootDesktopBackdrop.h"
#include "Music/BootMusicTypes.h"
#include "Player/BootFeedbackComponent.h"
#include "Profile/BootProfile.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
namespace
{
struct FFeedbackV04World
{
 UWorld* World; AActor* Actor; UBootRunEffectsComponent* Fx;
 FFeedbackV04World()
 {
  UWorld::InitializationValues Init; Init.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
  World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
  GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
  World->InitializeActorsForPlay(FURL());World->BeginPlay();World->GetWorldSettings()->NotifyBeginPlay();
  Actor=World->SpawnActor<AActor>();auto* Root=NewObject<USceneComponent>(Actor);Actor->AddInstanceComponent(Root);Actor->SetRootComponent(Root);Root->RegisterComponent();
  Fx=NewObject<UBootRunEffectsComponent>(Actor);Actor->AddInstanceComponent(Fx);Fx->RegisterComponent();
 }
 ~FFeedbackV04World(){World->EndPlay(EEndPlayReason::Quit);GEngine->DestroyWorldContext(World);World->DestroyWorld(false);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootEssentialV04,"Bootloop.V04.Feedback.EssentialTelegraphs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootEssentialV04::RunTest(const FString&)
{
 FFeedbackV04World S;FBootOptions O;O.EffectsIntensity=0.f;O.Sfx=0.f;O.bMotion=false;S.Fx->ApplyPreferences(O);
 S.Fx->Emit(EBootFeedbackEvent::BufferDash,FTransform::Identity,1.f);
 TestEqual(TEXT("FX0 removes ribbons"),S.Fx->GetActiveEffectCount(),0);
 S.Fx->Emit(EBootFeedbackEvent::Danger,FTransform::Identity,1.f);
 TestTrue(TEXT("warning at FX0"),S.Fx->HasEssentialWarning());
 TestTrue(TEXT("actual warning geometry created"),S.Fx->GetActiveEffectCount()>0);
 S.Fx->ApplyPreferences(O);TestTrue(TEXT("Reapplying FX0 preserves essential warning"),S.Fx->HasEssentialWarning());
 // Inspect the real Engine Plane UVs, rather than asserting the chosen rotation.
 TArray<UStaticMeshComponent*> WarningParts;S.Actor->GetComponents(WarningParts);
 bool bCheckedMask=false;
 for(auto* Part:WarningParts)
 {
  UStaticMesh* Mesh=Part->GetStaticMesh();
  if(!Part->IsVisible() || !Mesh || !Mesh->GetPathName().Contains(TEXT("/BasicShapes/Plane")))continue;
  const FStaticMeshRenderData* Render=Mesh->GetRenderData();
  if(!Render || Render->LODResources.IsEmpty())continue;
  const auto& LOD=Render->LODResources[0];FVector Top=FVector::ZeroVector,Bottom=FVector::ZeroVector,Left=FVector::ZeroVector,Right=FVector::ZeroVector;
  int32 Tops=0,Bottoms=0,Lefts=0,Rights=0;
  for(uint32 V=0;V<LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices();++V)
  {
   const FVector2f UV=LOD.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(V,0);
   const FVector P=Part->GetComponentTransform().TransformPosition(FVector(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(V)));
   if(UV.Y<.01f){Top+=P;++Tops;}if(UV.Y>.99f){Bottom+=P;++Bottoms;}
   if(UV.X<.01f){Left+=P;++Lefts;}if(UV.X>.99f){Right+=P;++Rights;}
  }
  if(Tops>0 && Bottoms>0 && Lefts>0 && Rights>0)
  {
   const FVector Vertical=Top/Tops-Bottom/Bottoms,Horizontal=Right/Rights-Left/Lefts;
   TestTrue(TEXT("Warning authored PNG top is world up"),Vertical.Z>20. && FMath::Abs(Vertical.Y)<.1);
   TestTrue(TEXT("Warning authored PNG right is view right"),Horizontal.Y>20. && FMath::Abs(Horizontal.Z)<.1);
   TestTrue(TEXT("Warning icon bounded28cm height"),FMath::IsNearlyEqual(Vertical.Size(),28.,.1));
   bCheckedMask=true;
  }
 }
 if(LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/V04/FX/M_RunFxMask.M_RunFxMask")))TestTrue(TEXT("Imported warning uses actual Engine Plane UVs"),bCheckedMask);
 S.Fx->ResetRun();TestFalse(TEXT("Retry clears warning"),S.Fx->HasEssentialWarning());
 TestFalse(TEXT("Idle effects have no Tick"),S.Fx->IsComponentTickEnabled());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootBoundedV04,"Bootloop.V04.Feedback.BoundedPool",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootBoundedV04::RunTest(const FString&)
{
 FFeedbackV04World S;FBootOptions O;O.Sfx=0.f;S.Fx->ApplyPreferences(O);
 for(int32 I=0;I<1000;++I)S.Fx->Emit(EBootFeedbackEvent::BufferDash,FTransform(FVector(I,0,0)),1.f);
 TestTrue(TEXT("Actual ribbons emitted"),S.Fx->GetActiveEffectCount()>0);
 TestTrue(TEXT("bounded FX"),S.Fx->GetActiveEffectCount()<=64);
 TestTrue(TEXT("bounded allocation plus contact chips"),S.Fx->GetPoolSize()<=40);
 S.Fx->Emit(EBootFeedbackEvent::Danger,FTransform::Identity,1.f);TestTrue(TEXT("warning cannot be starved by cosmetic saturation"),S.Fx->HasEssentialWarning());
 TArray<UStaticMeshComponent*> Parts;S.Actor->GetComponents(Parts);
 for(auto* P:Parts){TestEqual(TEXT("No decorative collision"),P->GetCollisionEnabled(),ECollisionEnabled::NoCollision);TestFalse(TEXT("No decorative overlaps"),P->GetGenerateOverlapEvents());}
 const int32 Allocated=S.Fx->GetPoolSize();
 for(int32 I=0;I<200;++I){S.Fx->ResetRun();S.Fx->Emit(EBootFeedbackEvent::GuardHit,FTransform::Identity,1.f);}
 TestEqual(TEXT("Retries reuse allocated components"),S.Fx->GetPoolSize(),Allocated);
 for(int32 I=0;I<60;++I)S.Fx->TickComponent(1.f/60,LEVELTICK_All,nullptr);
 TestEqual(TEXT("Finite effects expire"),S.Fx->GetActiveEffectCount(),0);TestFalse(TEXT("Expired effects stop Tick"),S.Fx->IsComponentTickEnabled());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootSupportV04,"Bootloop.V04.Feedback.SupportSound",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootSupportV04::RunTest(const FString&)
{
 const FBootTraversalEnvelope E=FBootTraversalPolicy::FromTempo(130.,1);
 FBootPoseInput Input;Input.WorldPlanarSpeed=1800.f;
 auto Mix=UBootFeedbackComponent::ComputeSupportSound(Input,E);
 TestTrue(TEXT("Moving base can create actual-speed wind"),Mix.Wind>0.f);
 TestEqual(TEXT("Moving base cannot create servo steps"),Mix.Servo,0.f);
 Input.LocalVelocity=FVector(1200,0,0);Input.bGrounded=false;Input.bSliding=true;
 Mix=UBootFeedbackComponent::ComputeSupportSound(Input,E);
 TestEqual(TEXT("Air slide preparation has no ground scrape"),Mix.Slide,0.f);
 Input.bGrounded=true;Mix=UBootFeedbackComponent::ComputeSupportSound(Input,E);
 TestTrue(TEXT("Supported slide has contact sound"),Mix.Slide>0.f);
 Input.bGrounded=false;Input.bSliding=false;Input.bWallRunning=true;
 TestEqual(TEXT("Wall intent without contact is silent"),UBootFeedbackComponent::ComputeSupportSound(Input,E).Wall,0.f);
 Input.bHasWallContact=true;TestTrue(TEXT("Actual wall contact has scrape"),UBootFeedbackComponent::ComputeSupportSound(Input,E).Wall>0.f);
 const FBootTraversalEnvelope Fast=FBootTraversalPolicy::FromTempo(168.,2);
 Input.WorldPlanarSpeed=E.TemporaryCmps;const float NormalWind=UBootFeedbackComponent::ComputeSupportSound(Input,E).Wind;
 Input.WorldPlanarSpeed=Fast.TemporaryCmps;TestTrue(TEXT("Wind uses authoritative tempo-normalized envelope"),FMath::IsNearlyEqual(NormalWind,UBootFeedbackComponent::ComputeSupportSound(Input,Fast).Wind));
 Input.WorldPlanarSpeed=0;TestEqual(TEXT("No wind for stationary runner"),UBootFeedbackComponent::ComputeSupportSound(Input,E).Wind,0.f);
 Input.WorldPlanarSpeed=std::numeric_limits<float>::quiet_NaN();TestEqual(TEXT("Invalid speed cannot produce wind"),UBootFeedbackComponent::ComputeSupportSound(Input,E).Wind,0.f);
 FFeedbackV04World S;FBootOptions O;O.Sfx=0.f;O.bMotion=false;S.Fx->ApplyPreferences(O);
 const FTransform Frame(FRotator(0,90,0),FVector(100,200,300));S.Fx->Emit(EBootFeedbackEvent::AirRedirect,Frame,1.f);
 TestTrue(TEXT("ALT-TAB shape emitted with motion disabled"),S.Fx->GetActiveEffectCount()>0);
 TArray<UStaticMeshComponent*> Parts;S.Actor->GetComponents(Parts);
 TArray<FVector> Locations;for(auto* P:Parts){TestTrue(TEXT("Finite actual mesh transform"),P->GetComponentTransform().IsValid());Locations.Add(P->GetComponentLocation());}
 S.Fx->TickComponent(.1f,LEVELTICK_All,nullptr);
 for(int32 I=0;I<Parts.Num();++I)TestTrue(TEXT("Reduced Motion keeps glyph position stable"),Parts[I]->GetComponentLocation().Equals(Locations[I],.001));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootDistrictV04,"Bootloop.V04.Feedback.DistrictLayers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBootDistrictV04::RunTest(const FString&)
{
 FFeedbackV04World S;auto* District=S.World->SpawnActor<ABootDesktopBackdrop>();
 District->ConfigureDistrict(FTransform::Identity,9504);District->SetFollowTarget(S.Actor);
 const int32 Parts=District->GetDecorativePartCount();TestTrue(TEXT("Layered genuine city geometry"),Parts>1000);
 FBootMusicContext Context;Context.TrackHash=TEXT("district-fixture");const auto Envelope=FBootTraversalPolicy::FromTempo(130.,1);
 for(int32 I=0;I<300;++I){Context.Section=I;Context.Energy=(I%5)/4.f;District->ApplyMusicContext(Context,Envelope);District->Tick(1.f/60);}
 TestEqual(TEXT("Music sections do not grow district geometry"),District->GetDecorativePartCount(),Parts);
 const FVector Position(170000,-80000,43000);S.Actor->SetActorLocation(Position);District->Tick(1.f/60);
 TestTrue(TEXT("Endless district follows full XYZ"),District->GetActorLocation().Equals(Position,.01));
 TArray<UPrimitiveComponent*> Geometry;District->GetComponents(Geometry);
 for(auto* P:Geometry){TestEqual(TEXT("City remains NoCollision"),P->GetCollisionEnabled(),ECollisionEnabled::NoCollision);TestFalse(TEXT("City never overlaps"),P->GetGenerateOverlapEvents());}
 return true;
}
#endif
