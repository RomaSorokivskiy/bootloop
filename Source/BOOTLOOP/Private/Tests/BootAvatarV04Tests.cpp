#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Player/BootAvatarPose.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAvatarV04Neutral,"Bootloop.V04.Avatar.NeutralProportions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootAvatarV04Neutral::RunTest(const FString&)
{
 FBootPoseInput Input; FBootAvatarPoseState State;
 const FBootAvatarPose Pose=FBootAvatarPoseSolver::Solve(Input,0.f,State);
 TestTrue(TEXT("Hip94cm above sole"),FMath::IsNearlyEqual(Pose.Pelvis.GetLocation().Z+96.,94.,.1));
 for(int32 Side=0;Side<2;++Side)
 {
  TestTrue(TEXT("fixed thigh"),FMath::IsNearlyEqual(FVector::Distance(Pose.Hips[Side],Pose.Knees[Side]),40.,.1));
  TestTrue(TEXT("fixed shin"),FMath::IsNearlyEqual(FVector::Distance(Pose.Knees[Side],Pose.Ankles[Side]),40.,.1));
  TestTrue(TEXT("Softly extended neutral knees"),FVector::Distance(Pose.Hips[Side],Pose.Ankles[Side])>78. && FVector::Distance(Pose.Hips[Side],Pose.Ankles[Side])<80.);
  TestTrue(TEXT("Feet below pelvis"),FMath::Abs(Pose.Ankles[Side].X-Pose.Hips[Side].X)<4.);
  TestTrue(TEXT("Relaxed wrists near hip below ribs"),Pose.Wrists[Side].Z<Pose.Pelvis.GetLocation().Z+8. && Pose.Wrists[Side].Z>Pose.Pelvis.GetLocation().Z-12.);
  TestTrue(TEXT("Shared shoulder frame"),FMath::IsNearlyEqual(Pose.Shoulders[Side].GetLocation().Z-Pose.Pelvis.GetLocation().Z,51.,.1));
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAvatarV04Transitions,"Bootloop.V04.Avatar.LocomotionTransitions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootAvatarV04Transitions::RunTest(const FString&)
{
 for(int32 Fps:{30,60,120})
 {
  FBootAvatarPoseState State; FBootPoseInput Input; Input.LocalVelocity=FVector(1000,0,0);
  bool bBackSwing=false; FVector Ascent=FVector::ZeroVector,Descent=FVector::ZeroVector;
  for(int32 Frame=0;Frame<Fps*8;++Frame)
  {
   const int32 Mode=Frame/Fps;
   Input.bGrounded=Mode<2 || Mode==4; Input.bSliding=Mode==4; Input.bWallRunning=Mode==5 || Mode==6;
   Input.WallSide=Mode==5?-1.f:1.f; Input.VerticalVelocity=Mode==2?500.f:Mode==3?-500.f:0.f;
   const FBootAvatarPose Pose=FBootAvatarPoseSolver::Solve(Input,1.f/Fps,State);
   TestTrue(TEXT("Shared torso finite"),Pose.Torso.IsValid() && Pose.Pelvis.IsValid());
   for(int32 Side=0;Side<2;++Side)
   {
    TestFalse(TEXT("Joints finite"),Pose.Knees[Side].ContainsNaN() || Pose.Wrists[Side].ContainsNaN());
    TestTrue(TEXT("fixed shin"),FMath::IsNearlyEqual(FVector::Distance(Pose.Knees[Side],Pose.Ankles[Side]),40.,.1));
    TestTrue(TEXT("fixed thigh"),FMath::IsNearlyEqual(FVector::Distance(Pose.Hips[Side],Pose.Knees[Side]),40.,.1));
    TestTrue(TEXT("fixed upper arm"),FMath::IsNearlyEqual(FVector::Distance(Pose.Shoulders[Side].GetLocation(),Pose.Elbows[Side]),28.,.1));
    TestTrue(TEXT("fixed forearm"),FMath::IsNearlyEqual(FVector::Distance(Pose.Elbows[Side],Pose.Wrists[Side]),30.,.1));
    const double Reach=FVector::Distance(Pose.Shoulders[Side].GetLocation(),Pose.Wrists[Side]);
    TestTrue(TEXT("Corrective arm reach bounded"),Reach<58. && Reach>10.);
    if(Mode==1 && Pose.Wrists[Side].X<Pose.Shoulders[Side].GetLocation().X-10.) bBackSwing=true;
   }
   if(Frame==Fps*3-1) Ascent=Pose.Ankles[0];
   if(Frame==Fps*4-1) Descent=Pose.Ankles[0];
  }
  TestTrue(TEXT("Jog contains natural backswing"),bBackSwing);
  TestTrue(TEXT("Ascent and descent differ"),FVector::Distance(Ascent,Descent)>4.);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBootAvatarV04Contact,"Bootloop.V04.Avatar.ContactAndVisibility",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBootAvatarV04Contact::RunTest(const FString&)
{
 for(int32 Fps:{30,60,120})
 {
  FBootPoseInput Input; FBootAvatarPoseState State; Input.WorldPlanarSpeed=1200.f;
  for(int32 I=0;I<Fps;++I)
   TestEqual(TEXT("Moving base does not create steps"),FBootAvatarPoseSolver::Solve(Input,1.f/Fps,State).ContactFoot,INDEX_NONE);
  Input.LocalVelocity=FVector(1000,0,0); int32 Contacts=0,Last=INDEX_NONE;
  for(int32 I=0;I<Fps*5;++I)
  {
   const FBootAvatarPose Pose=FBootAvatarPoseSolver::Solve(Input,1.f/Fps,State);
   if(Pose.ContactFoot!=INDEX_NONE){++Contacts;TestTrue(TEXT("Alternating feet"),Pose.ContactFoot!=Last);Last=Pose.ContactFoot;}
   TestTrue(TEXT("Brief support-relative contact window"),Pose.StanceSeconds>0.f && Pose.StanceSeconds<=.065f);
   for(int32 Side=0;Side<2;++Side)
   {
    // Forward centre excludes hands, even when naturally dropped below the viewport.
    const FVector P=Pose.Wrists[Side]-FVector(0,0,64);
    if(P.X>1.)
    {
     const double U=.5+P.Y/(2.*P.X*FMath::Tan(FMath::DegreesToRadians(47.5)));
     const double V=.5-P.Z/(2.*P.X*(FMath::Tan(FMath::DegreesToRadians(47.5))/(16./9.)));
     TestFalse(TEXT("Central landing corridor clear"),U>.4 && U<.6 && V>.32 && V<.62);
    }
   }
  }
  TestEqual(TEXT("Cadence independent of frame rate"),Contacts,20);
  const double Phase=State.GaitPhase;
  TestEqual(TEXT("Pause creates no contact"),FBootAvatarPoseSolver::Solve(Input,0.f,State).ContactFoot,INDEX_NONE);
  TestEqual(TEXT("Pause preserves gait phase"),State.GaitPhase,Phase);
 }
 return true;
}
#endif
