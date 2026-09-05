#pragma once

struct BgSpot03Taki;
struct EnDntDemo;
struct EnFu;
struct EnMa1;
struct PlayState;

void TimeSaver_EndEponaSongLesson(EnMa1* enMa1, PlayState* play);
void TimeSaver_EndStormsSongLesson(EnFu* enFu, PlayState* play);
void TimeSaver_ResolveDekuMaskJudgement(EnDntDemo* enDntDemo, PlayState* play);
void TimeSaver_KeepWaterfallOpen(BgSpot03Taki* bgSpot03Taki, PlayState* play);
