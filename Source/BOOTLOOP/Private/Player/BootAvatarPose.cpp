#include "Player/BootAvatarPose.h"

namespace
{
float Safe(float V,float Limit){return FMath::IsFinite(V)?FMath::Clamp(V,-Limit,Limit):0.f;}
float Blend(float A,float B,float Dt,float Rate=12.f){return FMath::Lerp(A,B,1.f-FMath::Exp(-Rate*Dt));}
void Limb(const FVector& Root,FVector& End,const FVector& Pole,float A,float B,FVector& Joint)
{
 FVector Axis=(End-Root).GetSafeNormal(); if(Axis.IsNearlyZero()) Axis=FVector(0,0,-1);
 const float Reach=FMath::Clamp(float(FVector::Distance(Root,End)),FMath::Abs(A-B)+.1f,A+B-.1f);
 End=Root+Axis*Reach;
 const float Along=(A*A-B*B+Reach*Reach)/(2.f*Reach);
 FVector Bend=(Pole-Axis*FVector::DotProduct(Pole,Axis)).GetSafeNormal();
 if(Bend.IsNearlyZero()) Bend=FVector::CrossProduct(Axis,FVector::RightVector).GetSafeNormal();
 Joint=Root+Axis*Along+Bend*FMath::Sqrt(FMath::Max(0.f,A*A-Along*Along));
}
}
FBootAvatarPose FBootAvatarPoseSolver::Solve(const FBootPoseInput& Input,float DeltaSeconds,FBootAvatarPoseState& State)
{
 FBootAvatarPose Pose;
 const float Dt=FMath::IsFinite(DeltaSeconds)?FMath::Clamp(DeltaSeconds,0.f,.1f):0.f;
 const FVector Velocity=Input.LocalVelocity.ContainsNaN()?FVector::ZeroVector:Input.LocalVelocity.GetClampedToMaxSize(10000.f);
 const FVector Acceleration=Input.LocalAcceleration.ContainsNaN()?FVector::ZeroVector:Input.LocalAcceleration.GetClampedToMaxSize(10000.f);
 const float Speed=FMath::Clamp(float(Velocity.Size2D()),0.f,3600.f);
 const float Pace=Input.NormalizedPace>=0.f?FMath::Clamp(Safe(Input.NormalizedPace,3.f),0.f,2.f):FMath::Clamp(Speed/1100.f,0.f,2.f);
 const bool bWalking=Input.bGrounded && !Input.bSliding && !Input.bWallRunning && Speed>80.f;
 const float Motion=Input.bMotionEnabled?1.f:.25f;
 const float HalfHeight=FMath::Clamp(Safe(Input.CapsuleHalfHeight,200.f),40.f,200.f);
 const float Crouch=FMath::Max(FMath::Clamp((96.f-HalfHeight)/52.f,0.f,1.f),Input.bSliding || Input.bCrouched?1.f:0.f);
 if(State.bInitialized && Dt>0.f)
 {
  if(State.bWasGrounded && !Input.bGrounded) State.TakeoffTime=.2f;
  if(!State.bWasGrounded && Input.bGrounded) State.LandingTime=.25f;
 }
 if(Dt>0.f)
 {
  State.bWasGrounded=Input.bGrounded; State.bInitialized=true;
  if(Input.AbilityEvent!=EBootPoseAbilityEvent::None){State.GestureTime=.34f;State.ActiveAbility=Input.AbilityEvent;}
  State.Clock+=Dt;
 }
 State.JogBlend=Blend(State.JogBlend,bWalking?FMath::Min(Pace,1.f):0.f,Dt);
 State.AirBlend=Blend(State.AirBlend,!Input.bGrounded && !Input.bWallRunning?1.f:0.f,Dt);
 State.SlideBlend=Blend(State.SlideBlend,Input.bSliding?1.f:0.f,Dt,16.f);
 State.WallBlend=Blend(State.WallBlend,Input.bWallRunning?1.f:0.f,Dt);
 State.CrouchBlend=Blend(State.CrouchBlend,Crouch,Dt,16.f);
 if(Input.bWallRunning && !FMath::IsNearlyZero(Input.WallSide)) State.LastWallSide=FMath::Sign(Safe(Input.WallSide,1.f));
 State.GestureTime=FMath::Max(0.f,State.GestureTime-Dt);
 State.TakeoffTime=FMath::Max(0.f,State.TakeoffTime-Dt);
 State.LandingTime=FMath::Max(0.f,State.LandingTime-Dt);
 const float Takeoff=FMath::Sin(State.TakeoffTime/.2f*PI)*Motion;
 const float Landing=FMath::Sin(State.LandingTime/.25f*PI)*Motion;
 const float Gesture=FMath::Sin(State.GestureTime/.34f*PI)*Motion;
 if(bWalking && Dt>0.f)
 {
  const double Next=State.GaitPhase+double(Dt)*FMath::Min(Speed/500.f,2.2f);
  const int64 Before=FMath::FloorToInt64(State.GaitPhase*2.+1.e-5);
  const int64 After=FMath::FloorToInt64(Next*2.+1.e-5);
  if(After>Before) Pose.ContactFoot=int32(After%2);
  State.GaitPhase=FMath::Fmod(Next,1.);
 }
 else if(!bWalking && Dt>0.f)
 {
  State.GaitPhase=0.;
  State.ContactAge[0]=State.ContactAge[1]=-1.f;
 }
 const float Phase=float(State.GaitPhase*2.*PI);
 const float Bob=-FMath::Cos(Phase*2.f)*.9f*State.JogBlend*Motion;
 const float Breath=FMath::Sin(float(FMath::Fmod(State.Clock,1000.))*2.1f)*.25f*(1.f-State.JogBlend)*Motion;
 const float HipZ=-HalfHeight+FMath::Lerp(94.f,55.f,State.CrouchBlend)+Bob-Landing*3.f+Takeoff*2.f;
 const float Lean=Safe(Acceleration.X/2200.f,4.f)*Motion;
 const float Roll=Safe(-Acceleration.Y/2500.f-Input.TurnRateDegrees/150.f,4.f)*Motion;
 const FQuat BodyRotation=FRotator(Lean-State.SlideBlend*18.f,0,Roll-State.LastWallSide*State.WallBlend*5.f).Quaternion();
 const FVector HipPosition(-12.f-State.SlideBlend*8.f,0,HipZ);
 Pose.Pelvis=FTransform(BodyRotation,HipPosition);
 Pose.Torso=FTransform(BodyRotation,Pose.Pelvis.TransformPosition(FVector(-2,0,29.f+Breath)));
 const FVector Travel=FVector(Velocity.X,Velocity.Y,0).GetSafeNormal(.0001f);
 Pose.StanceSeconds=FMath::Lerp(.06f,.035f,FMath::Clamp(Pace,0.f,1.f));
 const float AirTuck=FMath::Clamp(Safe(Input.VerticalVelocity,2500.f)/600.f,-1.f,1.f);
 for(int32 Side=0;Side<2;++Side)
 {
  const float Sign=Side==0?-1.f:1.f;
  const float T=FMath::Frac(float(State.GaitPhase)+Side*.5f);
  const float CycleRate=FMath::Min(Speed/500.f,2.2f);
  const float ContactFraction=FMath::Clamp(Pose.StanceSeconds*CycleRate,.03f,.14f);
  const bool bStance=T<ContactFraction;
  const float SwingT=FMath::Clamp((T-ContactFraction)/(1.f-ContactFraction),0.f,1.f);
  const float Stride=(bStance?FMath::Lerp(1.f,-1.f,T/ContactFraction):FMath::Lerp(-1.f,1.f,SwingT*SwingT*(3.f-2.f*SwingT)))*17.f*State.JogBlend;
  const float Lift=bStance?0.f:FMath::Sin(SwingT*PI)*13.f*State.JogBlend;
  Pose.Hips[Side]=Pose.Pelvis.TransformPosition(FVector(0,Sign*12.f,0));
  FVector Ankle(-12.f,Sign*12.f,-HalfHeight+15.f+Lift);
  Ankle+=Travel*Stride;
  if(Pose.ContactFoot==Side)
  {
   State.ContactAnchors[Side]=Ankle;
   State.ContactAge[Side]=0.f;
  }
  else if(State.ContactAge[Side]>=0.f && Dt>0.f)
  {
   State.ContactAge[Side]+=Dt;
   State.ContactAnchors[Side]-=Velocity*Dt;
  }
  Pose.bContactActive[Side]=bWalking && State.ContactAge[Side]>=0.f && State.ContactAge[Side]<=Pose.StanceSeconds;
  if(Pose.bContactActive[Side]) Ankle=State.ContactAnchors[Side];
  Ankle=FMath::Lerp(Ankle,FVector(2.f+Side*8.f,Sign*15.f,-HalfHeight+29.f+AirTuck*10.f+Side*AirTuck*5.f+Takeoff*7.f),State.AirBlend);
  Ankle=FMath::Lerp(Ankle,FVector(Side==0?49.f:9.f,Sign*17.f,-HalfHeight+(Side==0?16.f:29.f)),State.SlideBlend);
  Ankle=FMath::Lerp(Ankle,FVector(4.f+Side*10.f,Sign*22.f,-HalfHeight+27.f+Side*7.f),State.WallBlend);
  if(State.ActiveAbility==EBootPoseAbilityEvent::WallKick) Ankle+=FVector(-Gesture*12.f,-State.LastWallSide*Gesture*8.f,Gesture*9.f);
  Limb(Pose.Hips[Side],Ankle,FVector(1,Sign*.08f,.05f),40.f,40.f,Pose.Knees[Side]);
  Pose.Ankles[Side]=Ankle;
  Pose.Boots[Side]=FRotator(-FMath::Sin(SwingT*PI)*State.JogBlend*8.f+State.SlideBlend*10.f,Sign*State.SlideBlend*6.f,0);
  Pose.Shoulders[Side]=FTransform(BodyRotation,Pose.Pelvis.TransformPosition(FVector(-2.f,Sign*22.f,51.f)));
  const FVector Shoulder=Pose.Shoulders[Side].GetLocation();
  const float Swing=FMath::Cos(Phase+Side*PI)*State.JogBlend;
  // Idle hands settle near the pelvis; jogging elbows flex and each arm travels behind its shoulder.
  FVector Wrist=Shoulder+BodyRotation.RotateVector(FVector(-3.f-Swing*26.f,Sign*3.f,-52.f+State.JogBlend*16.f+Swing*5.f));
  Wrist=FMath::Lerp(Wrist,Shoulder+FVector(9.f+Side*5.f,Sign*7.f,-35.f+AirTuck*7.f),State.AirBlend);
  Wrist=FMath::Lerp(Wrist,Shoulder+FVector(11.f,Sign*13.f,-30.f+Side*7.f),State.SlideBlend);
  FRotator Hand(-75.f+State.JogBlend*29.f+Swing*12.f,Sign*-5.f,Sign*8.f);
  if(Sign==State.LastWallSide)
  {
   FVector Contact=Shoulder+FVector(27.f,Sign*25.f,0);
   if(Input.bHasWallContact && !Input.LocalWallContact.ContainsNaN()) Contact=Input.LocalWallContact;
   Wrist=FMath::Lerp(Wrist,Contact,State.WallBlend);
   Hand=FMath::Lerp(Hand,FRotator(5,0,-Sign*75.f),State.WallBlend);
  }
  else Wrist+=FVector(-State.WallBlend*8.f,0,State.WallBlend*8.f);
  if(State.ActiveAbility==EBootPoseAbilityEvent::Dash) Wrist+=FVector(-Gesture*14.f,Sign*Gesture*3.f,Gesture*4.f);
  else if(State.ActiveAbility==EBootPoseAbilityEvent::WallKick) Wrist+=FVector(-Gesture*5.f,-State.LastWallSide*Gesture*10.f,Gesture*8.f);
  else if(Side==0) Wrist+=FVector(Gesture*22.f,Gesture*4.f,Gesture*14.f);
  Limb(Shoulder,Wrist,FVector(-.4f,Sign,-.5f),28.f,30.f,Pose.Elbows[Side]);
  Pose.Wrists[Side]=Wrist; Pose.Hands[Side]=Hand;
 }
 return Pose;
}

