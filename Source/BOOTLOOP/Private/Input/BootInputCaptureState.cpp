#include "Input/BootInputCaptureState.h"
void FBootInputCaptureState::Begin(EBootInputAction InAction,int32 InSlot,FKey Opener)
{
    CancelCapture();
    if(int32(InAction)>=int32(EBootInputAction::Count) || InSlot<0 || InSlot>1) return;
    Action=int32(InAction); Slot=InSlot; OpeningKey=Opener; bArmed=!Opener.IsValid();
}
void FBootInputCaptureState::Release(FKey Key) { if(IsCapturing() && Key==OpeningKey) bArmed=true; }
EBootBindingResult FBootInputCaptureState::Press(FKey Key,bool bRepeat)
{
    if(!IsCapturing() || bRepeat) return EBootBindingResult::Invalid;
    if(Key==EKeys::Escape) { CancelCapture(); return EBootBindingResult::Invalid; }
    if(!bArmed || bConflict) return EBootBindingResult::Invalid;
    auto Result=Draft.Assign(EBootInputAction(Action),Slot,Key,false);
    if(Result==EBootBindingResult::Conflict) { PendingKey=Key; bConflict=true; }
    else if(Result==EBootBindingResult::Applied) CancelCapture();
    return Result;
}
bool FBootInputCaptureState::Swap()
{
    if(!IsCapturing() || !bConflict) return false;
    if(Draft.Assign(EBootInputAction(Action),Slot,PendingKey,true)!=EBootBindingResult::Applied) return false;
    CancelCapture(); return true;
}
void FBootInputCaptureState::CancelCapture()
{ Action=INDEX_NONE; Slot=0; bArmed=false; bConflict=false; OpeningKey=FKey(); PendingKey=FKey(); }
