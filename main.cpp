/*
  Feed It!
  Copyright (C) 2020 Bernhard Schelling

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include <ZL_Application.h>
#include <ZL_Display.h>
#include <ZL_Surface.h>
#include <ZL_Signal.h>
#include <ZL_Audio.h>
#include <ZL_Font.h>
#include <ZL_Input.h>
#include <ZL_SynthImc.h>
#include <../Opt/chipmunk/chipmunk.h>

extern TImcSongData imcDataIMCHIT, imcDataIMCEAT, imcDataIMCBOING, imcDataIMCGAMEOVER, imcDataIMCCLEAR, imcDataIMCTOGGLE, imcDataIMCPOISON;
extern ZL_SynthImcTrack imcMusic;
static ZL_Sound sndHit, sndEat, sndBoing, sndGameOver, sndClear, sndToggle, sndPoison;
static ZL_Font fntMain;
static cpSpace *space;
static cpBody *mouseBody;
static cpConstraint *mouseJoint;
static ZL_Surface srfFood, srfPoison, srfMonster, srfEat, srfBelt[3], srfWall, srfLever, srfBumper;
static std::vector<cpVect> spawns;
static ticks_t tickNextSpawn, tickLastEat;
static int stage, foodNeed, foodLeft, boxesOnScreen;
static ZL_Color bg[] = { ZLBLACK, ZLBLACK, ZLBLACK, ZLBLACK };
static ZL_Color colShadow = ZLLUMA(0, .5);
static ZL_TextBuffer txtStageX, txtFoodNeedX, txtFoodLeftX;

enum GameMode
{
	MODE_TITLE,
	MODE_PLAY,
	MODE_CLEAR,
	MODE_GAMEOVER,
	MODE_PAUSE,
	MODE_FINISH,
};
static GameMode mode = MODE_TITLE;
static ticks_t modeTick;

enum CollisionTypes
{
	COLLISION_NONE,
	COLLISION_MONSTER,
	COLLISION_BOX,
	COLLISION_LEVER,
	COLLISION_BELT,
	COLLISION_BUMPER,
	COLLISION_WALL,
};

#define GRABBABLE_MASK_BIT (unsigned int)(1<<1)
static cpShapeFilter GRABBABLE_FILTER     = {CP_NO_GROUP, GRABBABLE_MASK_BIT, GRABBABLE_MASK_BIT};
static cpShapeFilter NOT_GRABBABLE_FILTER = {CP_NO_GROUP, ~GRABBABLE_MASK_BIT, ~GRABBABLE_MASK_BIT};

static void DrawTextBordered(const ZL_TextBuffer& buf, const ZL_Vector& p, scalar scale = 1, const ZL_Color& colfill = ZLWHITE, const ZL_Color& colborder = ZLBLACK, int border = 2, ZL_Origin::Type origin = ZL_Origin::Center)
{
	for (int i = 0; i < 9; i++) if (i != 4) buf.Draw(p.x+(border*((i%3)-1)), p.y+(border*((i/3)-1)), scale, scale, colborder, origin);
	buf.Draw(p.x, p.y, scale, scale, colfill, origin);
}

static void DrawTextShadowed(const ZL_TextBuffer& buf, const ZL_Vector& p, scalar scale = 1, const ZL_Color& colfill = ZLWHITE, const ZL_Color& colshadow = ZLLUMA(0,.5), int dist = 3, ZL_Origin::Type origin = ZL_Origin::BottomLeft)
{
	buf.Draw(p.x + dist, p.y - dist, scale, scale, colshadow, origin);
	buf.Draw(p.x, p.y, scale, scale, colfill, origin);
}

static void UpdateFoodNeed(int v) { foodNeed = v; txtFoodNeedX.SetText(ZL_String::format("Need %d Banana box%s to stay alive", foodNeed, (foodNeed == 1 ? "" : "es"))); }
static void UpdateFoodLeft(int v) { foodLeft = v; txtFoodLeftX.SetText(ZL_String::format("%d Banana box%s to be delivered", foodLeft, (foodLeft == 1 ? "" : "es"))); }
static void UpdateStage(int v)    { stage    = v; txtStageX.SetText(ZL_String::format("Stage %d", stage)); }

static void MakeLever(cpVect pos, bool right)
{
	cpFloat mass = 100.0f;
	cpVect p1 = cpv(0,  0);
	cpVect p2 = cpv(100, 0);
	
	cpBody* b = cpSpaceAddBody(space, cpBodyNew(mass, cpMomentForSegment(mass, p1, p2, 0.0f)));
	cpBodySetAngle(b, CP_PI/4*(right ? 1 : 3));
	//b->a = CP_PI/4*6;
	
	cpBodySetPosition(b, pos);

	cpShape *shape = cpSpaceAddShape(space, cpSegmentShapeNew(b, p1, p2, 5.0f));
	cpShapeSetElasticity(shape, 0.0f);
	cpShapeSetFriction(shape, 0.1f);
	cpShapeSetFilter(shape, NOT_GRABBABLE_FILTER);
	cpShapeSetCollisionType(shape, COLLISION_LEVER);

	cpSpaceAddConstraint(space, cpRotaryLimitJointNew(b, space->staticBody, CP_PI/4*-3, CP_PI/4*-1));
	cpSpaceAddConstraint(space, cpPivotJointNew(b, space->staticBody, pos));

	cpBody *head = cpSpaceAddBody(space, cpBodyNew(10.0f, INFINITY));
	cpBodySetPosition(head, cpTransformPoint(b->transform, cpSegmentShapeGetB(shape)));
	cpShape *headshape = cpSpaceAddShape(space, cpCircleShapeNew(head, 1.0f, cpvzero));
	cpShapeSetFilter(headshape, GRABBABLE_FILTER);
	cpSpaceAddConstraint(space, cpPivotJointNew(b, head, head->p))->collideBodies = false;

}

static void MakeBelt(cpVect pos, float a, bool flip)
{
	cpFloat mass = 100.0f;
	cpVect p1 = cpv(-60, 0);
	cpVect p2 = cpv( 60, 0);
	if (flip) std::swap(p1, p2);
	
	cpBody* b = cpSpaceAddBody(space, cpBodyNewStatic());
	cpBodySetPosition(b, pos);
	cpBodySetAngle(b, a);

	cpShape *shape = cpSpaceAddShape(space, cpSegmentShapeNew(b, p1, p2, 5.0f));
	cpShapeSetElasticity(shape, 0.0f);
	cpShapeSetFriction(shape, 0.f);
	cpShapeSetFilter(shape, NOT_GRABBABLE_FILTER);
	cpShapeSetCollisionType(shape, COLLISION_BELT);
	cpShapeSetUserData(shape, (cpDataPointer)flip);
}

static void MakeBumper(cpVect pos)
{
	cpBody* b = cpSpaceAddBody(space, cpBodyNewStatic());
	cpBodySetPosition(b, pos);

	cpShape *shape = cpSpaceAddShape(space, cpCircleShapeNew(b, 50, cpvzero));
	cpShapeSetElasticity(shape, 0.0f);
	cpShapeSetFriction(shape, 0.f);
	cpShapeSetFilter(shape, NOT_GRABBABLE_FILTER);
	cpShapeSetCollisionType(shape, COLLISION_BUMPER);
}

static void MakeWall(cpVect pos, float a)
{
	cpFloat mass = 100.0f;
	cpVect p1 = cpv(-60, 0);
	cpVect p2 = cpv( 60, 0);
	
	cpBody* b = cpSpaceAddBody(space, cpBodyNewStatic());
	cpBodySetPosition(b, pos);
	cpBodySetAngle(b, a);

	cpShape *shape = cpSpaceAddShape(space, cpSegmentShapeNew(b, p1, p2, 5.0f));
	cpShapeSetElasticity(shape, 0.0f);
	cpShapeSetFriction(shape, 0.f);
	cpShapeSetFilter(shape, NOT_GRABBABLE_FILTER);
	cpShapeSetCollisionType(shape, COLLISION_WALL);
}

static void MakeMonster(cpVect pos)
{
	cpBody* b = cpSpaceAddBody(space, cpBodyNewStatic());
	cpBodySetPosition(b, pos);
	cpBodySetAngle(b, -CP_PI/2);
	cpShape *shape = cpSpaceAddShape(space, cpBoxShapeNew(b, 80, 80, 5));
	cpShapeSetCollisionType(shape, COLLISION_MONSTER);
	cpShapeSetFilter(shape, NOT_GRABBABLE_FILTER);
}

static void SpawnBox(cpVect pos)
{
	if (foodLeft <= 0 && mode != MODE_TITLE) return;
	cpBody *b = cpSpaceAddBody(space, cpBodyNew(50, cpMomentForCircle(50, 0, 25, cpvzero)));
	cpBodySetPosition(b, pos);
	cpBodySetAngle(b, -CP_PI/2);
	cpShape *shape = cpSpaceAddShape(space, cpBoxShapeNew(b, 50, 50, 5));
	cpShapeSetFriction(shape, 1);
	cpShapeSetCollisionType(shape, COLLISION_BOX);
	cpShapeSetFilter(shape, NOT_GRABBABLE_FILTER);
	cpShapeSetUserData(shape, (cpDataPointer)RAND_BOOL);
	cpBodySetAngularVelocity(b, 0);
	if (!shape->userData) UpdateFoodLeft(foodLeft - 1);
}

static void PostStepRemoveBody(cpSpace *space, cpBody *key, void *data)
{
	CP_BODY_FOREACH_SHAPE(key, shape) cpSpaceRemoveShape(space, shape);
	cpSpaceRemoveBody(space, key);
}

static cpBool CollisionBoxToMonster(cpArbiter *arb, cpSpace *space, cpDataPointer userData)
{
	CP_ARBITER_GET_SHAPES(arb, sa, sb);
	UpdateFoodNeed(foodNeed - (sa->userData ? -1 : 1));
	cpSpaceAddPostStepCallback(space, (cpPostStepFunc)PostStepRemoveBody, sa->body, NULL);
	(sa->userData ? sndPoison : sndEat).Play();
	tickLastEat = ZLTICKS;
	return cpFalse;
}

static void CollisionBoxToBelt(cpArbiter *arb, cpSpace *space, cpDataPointer userData)
{
	CP_ARBITER_GET_SHAPES(arb, sa, sb);
	cpSegmentShape* beltShape = (cpSegmentShape*)sb;
	cpBodyApplyForceAtWorldPoint(sa->body, cpvmult(cpvperp(beltShape->n), -10000.f), sa->body->p);
}

static cpBool CollisionBoxToBumper(cpArbiter *arb, cpSpace *space, cpDataPointer userData)
{
	CP_ARBITER_GET_SHAPES(arb, sa, sb);
	cpSegmentShape* beltShape = (cpSegmentShape*)sb;
	cpBodyApplyForceAtWorldPoint(sa->body, cpvmult(cpvnormalize(cpvsub(sa->body->p, sb->body->p)), 1500000.f), sa->body->p);
	sndBoing.Play();
	return cpTrue;
}

static cpBool CollisionMakeSound(cpArbiter *arb, cpSpace *space, cpDataPointer userData)
{
	sndHit.Play();
	return cpTrue;
}

static void StartLevel(int startstage)
{
	if (space)
	{
		spawns.clear();
		cpSpaceDestroy(space);
		mouseJoint = NULL;
	}

	space = cpSpaceNew();
	cpSpaceSetGravity(space, cpv(0.0f, -98.7f));
	cpSpaceAddCollisionHandler(space, COLLISION_BOX, COLLISION_MONSTER)->beginFunc = CollisionBoxToMonster;
	cpSpaceAddCollisionHandler(space, COLLISION_BOX, COLLISION_BELT)->beginFunc = CollisionMakeSound;
	cpSpaceAddCollisionHandler(space, COLLISION_BOX, COLLISION_BELT)->postSolveFunc = CollisionBoxToBelt;
	cpSpaceAddCollisionHandler(space, COLLISION_BOX, COLLISION_BUMPER)->beginFunc = CollisionBoxToBumper;
	cpSpaceAddCollisionHandler(space, COLLISION_BOX, COLLISION_WALL)->beginFunc = CollisionMakeSound;
	cpSpaceAddCollisionHandler(space, COLLISION_BOX, COLLISION_LEVER)->beginFunc = CollisionMakeSound;
	mouseBody = cpBodyNewKinematic();

	if (startstage == 0)
	{
		// TITLE
		spawns.push_back(cpv(497.000000f, 720.000000f));
		spawns.push_back(cpv(-516.000000f, 720.000000f));
		MakeLever(cpv(643.000122f, -7.000000f), false);
		MakeLever(cpv(-643.000305f, -3.000001f), true);
		MakeWall(cpv(-376.000000f, 453.000000f), 1.570796f);
		MakeWall(cpv(-376.000000f, 570.000000f), 1.570796f);
		MakeBelt(cpv(-313.000000f, 618.000000f), 0.000000f, true);
		MakeMonster(cpv(-19.000000f, 317.000000f));
		MakeBelt(cpv(-312.000000f, 521.000000f), 0.000000f, false);
		MakeWall(cpv(-180.000000f, 570.000000f), 1.570796f);
		MakeWall(cpv(-181.000000f, 450.000000f), 1.570796f);
		MakeBelt(cpv(-118.000000f, 621.000000f), 0.000000f, false);
		MakeBelt(cpv(-118.000000f, 520.000000f), 0.000000f, false);
		MakeBelt(cpv(-115.000000f, 407.000000f), 0.000000f, true);
		MakeWall(cpv(27.000000f, 570.000000f), 1.570796f);
		MakeWall(cpv(28.000000f, 455.000000f), 1.570796f);
		MakeWall(cpv(261.000000f, 623.000000f), 0.000000f);
		MakeWall(cpv(332.000000f, 574.000000f), 1.963495f);
		MakeWall(cpv(259.000000f, 408.000000f), 0.000000f);
		MakeWall(cpv(333.000000f, 459.000000f), 1.178097f);
		MakeBelt(cpv(97.000000f, 621.000000f), 0.000000f, true);
		MakeBelt(cpv(96.000000f, 523.000000f), 0.000000f, true);
		MakeBelt(cpv(97.000000f, 407.000000f), 0.000000f, false);
		MakeWall(cpv(209.000000f, 458.000000f), 1.570796f);
		MakeWall(cpv(209.000000f, 572.000000f), 1.570796f);
		MakeWall(cpv(-150.000000f, 202.000000f), 1.570796f);
		MakeWall(cpv(-151.000000f, 263.000000f), 0.000000f);
		MakeWall(cpv(29.000000f, 197.000000f), 1.570796f);
		MakeWall(cpv(8.000000f, 263.000000f), 0.000000f);
		MakeWall(cpv(-152.000000f, 54.000000f), 0.000000f);
		MakeWall(cpv(-150.000000f, 111.000000f), 1.570796f);
		MakeWall(cpv(67.000000f, 263.000000f), 0.000000f);
		MakeWall(cpv(29.000000f, 105.000000f), 1.570796f);
		MakeBumper(cpv(624.000000f, 713.000000f));
		MakeBumper(cpv(-621.000000f, 715.000000f));
	}
	else if (startstage == 1)
	{
		spawns.push_back(cpv(-109.000000f, 675.000000f));
		MakeLever(cpv(-110.000603f, 282.999329f), false);
		MakeWall(cpv(-353.000000f, 85.000000f), 1.570796f);
		MakeWall(cpv(-193.000000f, 419.000000f), 1.570796f);
		MakeWall(cpv(-59.000000f, 228.000000f), -0.785398f);
		MakeWall(cpv(-158.000000f, 232.000000f), 0.785398f);
		MakeWall(cpv(-315.000000f, 244.000000f), 0.785398f);
		MakeWall(cpv(-304.000000f, 24.000000f), 0.000000f);
		MakeWall(cpv(-233.000000f, 325.000000f), 0.785398f);
		MakeWall(cpv(97.000000f, 241.000000f), 2.356194f);
		MakeWall(cpv(-253.000000f, 24.000000f), 0.000000f);
		MakeWall(cpv(-26.000000f, 415.000000f), -1.570796f);
		MakeWall(cpv(15.000000f, 323.000000f), -0.785398f);
		MakeWall(cpv(-353.000000f, 147.000000f), 1.570796f);
		MakeWall(cpv(138.000000f, 144.000000f), 1.570796f);
		MakeWall(cpv(-198.000000f, 138.000000f), 1.570796f);
		MakeMonster(cpv(-275.000000f, 77.000000f));
		MakeWall(cpv(-19.000000f, 132.000000f), 1.570796f);
		MakeWall(cpv(159.000000f, 34.000000f), -1.178097f);
		MakeWall(cpv(-40.000000f, 27.000000f), -1.963495f);
		MakeWall(cpv(-198.000000f, 75.000000f), -1.570796f);
		UpdateFoodNeed(5);
		UpdateFoodLeft(15);
	}
	else if (startstage == 2)
	{
		spawns.push_back(cpv(-350.f, 675.f));
		MakeLever(cpv(-335.055511f, 485.944489f), false);
		MakeLever(cpv(-245.024567f, 404.975342f), false);
		MakeWall(cpv(-117.f, 254.f), 1.963495f);
		MakeWall(cpv(424.f, 243.f), -0.785398f);
		MakeWall(cpv(315.f, 666.f), -0.392699f);
		MakeWall(cpv(203.f, 688.f), 0.f);
		MakeWall(cpv(424.f, 244.f), 0.785398f);
		MakeWall(cpv(-221.f, 257.f), 1.570796f);
		MakeWall(cpv(84.f, 688.f), 0.f);
		MakeWall(cpv(-178.f, 345.f), 5.497787f);
		MakeWall(cpv(425.f, 199.f), 0.f);
		MakeWall(cpv(-35.f, 688.f), 0.f);
		MakeWall(cpv(-162.f, 206.f), 0.f);
		MakeWall(cpv(426.f, 160.f), 0.785398f);
		MakeWall(cpv(-151.f, 688.f), 0.f);
		MakeMonster(cpv(421.f, 343.f));
		MakeWall(cpv(418.f, 160.f), 2.356194f);
		MakeWall(cpv(-221.f, 333.f), 1.570796f);
		MakeWall(cpv(422.f, 112.f), 0.f);
		MakeWall(cpv(427.f, 289.f), 3.141593f);
		MakeWall(cpv(490.f, 399.f), 1.570796f);
		MakeWall(cpv(372.f, 56.f), 1.570796f);
		MakeWall(cpv(467.f, 511.f), 1.963495f);
		MakeWall(cpv(475.f, 57.f), 1.570796f);
		MakeWall(cpv(490.f, 339.f), 1.570796f);
		MakeWall(cpv(-220.f, 639.f), 1.570796f);
		MakeWall(cpv(-260.f, 687.f), 0.f);
		MakeWall(cpv(405.f, 606.f), 2.356194f);
		MakeWall(cpv(-258.f, 646.f), 2.356194f);
		UpdateFoodNeed(10);
		UpdateFoodLeft(20);
	}
	else if (startstage == 3)
	{
		spawns.push_back(cpv(-110.f, 675.f));
		spawns.push_back(cpv(308.f, 675.f));
		MakeLever(cpv(-105.000038f, 445.999847f), true);
		MakeLever(cpv(317.000977f, 430.999023f), false);
		MakeWall(cpv(277.f, 370.f), 0.785398f);
		MakeWall(cpv(299.f, 311.f), -0.392699f);
		MakeWall(cpv(179.f, 688.f), 0.785398f);
		MakeWall(cpv(98.f, 486.f), 1.570796f);
		MakeWall(cpv(168.f, 143.f), 1.963495f);
		MakeWall(cpv(-131.f, 245.f), -1.570796f);
		MakeWall(cpv(75.f, 598.f), 1.963495f);
		MakeBelt(cpv(114.f, 210.f), 0.f, true);
		MakeBelt(cpv(-6.f, 89.f), -6.675885f, true);
		MakeWall(cpv(95.f, 67.f), 0.f);
		MakeWall(cpv(14.f, 691.f), 2.356194f);
		MakeWall(cpv(-62.f, 386.f), -0.785398f);
		MakeWall(cpv(-99.f, 370.f), 1.570796f);
		MakeWall(cpv(-82.f, 325.f), 0.392699f);
		MakeWall(cpv(146.f, 135.f), 1.570796f);
		MakeWall(cpv(338.f, 346.f), 1.963495f);
		MakeWall(cpv(205.f, 54.f), -1.178097f);
		MakeWall(cpv(118.f, 597.f), 1.178097f);
		MakeBelt(cpv(-93.f, 148.f), -0.785398f, true);
		MakeMonster(cpv(86.f, 119.f));
		UpdateFoodNeed(5);
		UpdateFoodLeft(15);
	}
	else if (startstage == 4)
	{
		spawns.push_back(cpv(-152.f, 675.f));
		spawns.push_back(cpv(-5.f, 675.f));
		spawns.push_back(cpv(177.f, 675.f));
		MakeLever(cpv(-77.002007f, 21.994591f), false);
		MakeLever(cpv(33.000019f, 17.999990f), true);
		MakeWall(cpv(564.f, 194.f), 1.570796f);
		MakeWall(cpv(-284.f, 226.f), -0.785398f);
		MakeWall(cpv(467.f, 11.f), 0.f);
		MakeWall(cpv(-360.f, 643.f), 0.392699f);
		MakeWall(cpv(564.f, 314.f), 1.570796f);
		MakeWall(cpv(564.f, 434.f), 1.570796f);
		MakeBumper(cpv(-349.f, 323.f));
		MakeWall(cpv(564.f, 541.f), 1.570796f);
		MakeWall(cpv(527.f, 627.f), 2.356194f);
		MakeBumper(cpv(-426.f, 555.f));
		MakeWall(cpv(431.f, 668.f), 0.f);
		MakeBumper(cpv(386.f, 585.f));
		MakeBumper(cpv(230.f, 439.f));
		MakeWall(cpv(338.f, 706.f), 2.356194f);
		MakeMonster(cpv(491.f, 65.f));
		MakeWall(cpv(-201.f, 143.f), 2.356194f);
		MakeWall(cpv(320.f, 132.f), 1.963495f);
		MakeWall(cpv(-270.f, 703.f), 0.785398f);
		MakeBumper(cpv(27.f, 408.f));
		MakeWall(cpv(154.f, 144.f), 0.785398f);
		MakeWall(cpv(-406.f, 436.f), 1.963495f);
		MakeWall(cpv(514.f, 11.f), 0.f);
		MakeWall(cpv(248.f, 183.f), -0.f);
		MakeWall(cpv(564.f, 79.f), 1.570796f);
		MakeWall(cpv(378.f, 45.f), -0.785398f);
		UpdateFoodNeed(10);
		UpdateFoodLeft(30);
	}
	else if (startstage == 5)
	{
		spawns.push_back(cpv(-164.f, 675.f));
		spawns.push_back(cpv(-17.f, 675.f));
		spawns.push_back(cpv(127.f, 675.f));
		MakeBelt(cpv(-192.f, 541.f), 0.f, true);
		MakeBelt(cpv(178.f, 541.f), 0.f, false);
		MakeBelt(cpv(-100.f, 435.f), 0.f, true);
		MakeBelt(cpv(121.f, 435.f), 0.f, false);
		MakeBelt(cpv(-223.f, 326.f), 0.f, false);
		MakeBelt(cpv(15.f, 326.f), 0.f, true);
		MakeBelt(cpv(265.f, 326.f), 0.f, true);
		MakeBelt(cpv(-331.f, 232.f), 0.f, false);
		MakeBelt(cpv(-116.f, 232.f), 0.f, false);
		MakeBelt(cpv(126.f, 232.f), 0.f, true);
		MakeBelt(cpv(363.f, 232.f), 0.f, true);
		MakeBelt(cpv(-227.f, 103.f), 0.f, false);
		MakeBelt(cpv(7.f, 103.f), 0.f, true);
		MakeBelt(cpv(261.f, 103.f), 0.f, true);
		MakeBumper(cpv(-484.f, 106.f));
		MakeBumper(cpv(504.f, 97.f));
		MakeWall(cpv(-111.f, -4.f), 0.f);
		MakeMonster(cpv(-111.f, 47.f));
		UpdateFoodNeed(10);
		UpdateFoodLeft(30);
	}
	else
	{
		// game cleared
		MakeMonster(cpv(-250.f, ZLHALFH+20.f));
		MakeMonster(cpv(-125.f, ZLHALFH+20.f));
		MakeMonster(cpv(   0.f, ZLHALFH+20.f));
		MakeMonster(cpv( 125.f, ZLHALFH+20.f));
		MakeMonster(cpv( 250.f, ZLHALFH+20.f));
	}

	if (!stage || stage != startstage)
		bg[0] = RAND_COLOR*.5f, bg[1] = RAND_COLOR*.5f, bg[2] = RAND_COLOR*.5f, bg[3] = RAND_COLOR*.5f;

	tickLastEat = 0;
	tickNextSpawn = ZLTICKS + 2000;
	mode = (startstage > 5 ? MODE_FINISH : (startstage == 0 ? MODE_TITLE : MODE_PLAY));
	modeTick = ZLTICKS;
	UpdateStage(startstage);
	imcMusic.SetSongVolume(startstage == 0 ? 100 : 60);
}

static void Init()
{
	fntMain = ZL_Font("Data/MonkirtaPursuitNC.ttf.zip", 52);
	txtStageX    = ZL_TextBuffer(fntMain);
	txtFoodNeedX = ZL_TextBuffer(fntMain);
	txtFoodLeftX = ZL_TextBuffer(fntMain);

	srfFood      = ZL_Surface("Data/food.png");
	srfPoison    = ZL_Surface("Data/poison.png");
	srfMonster   = ZL_Surface("Data/monster.png");
	srfEat       = ZL_Surface("Data/eat.png");
	srfBelt[0]   = ZL_Surface("Data/belt1.png");
	srfBelt[1]   = ZL_Surface("Data/belt2.png");
	srfBelt[2]   = ZL_Surface("Data/belt3.png");
	srfWall      = ZL_Surface("Data/wall.png");
	srfLever     = ZL_Surface("Data/lever.png");
	srfBumper    = ZL_Surface("Data/bumper.png").SetOrigin(ZL_Origin::Center).SetScale(.4f);

	sndHit = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCHIT);
	sndEat = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCEAT);
	sndBoing = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCBOING);
	sndGameOver = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCGAMEOVER);
	sndClear = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCCLEAR);
	sndToggle = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCTOGGLE);
	sndPoison = ZL_SynthImcTrack::LoadAsSample(&imcDataIMCPOISON);
	imcMusic.Play();

	StartLevel(0);
}

static void DrawThing(cpShape *shape, const ZL_Color* color)
{
	if (shape->type == COLLISION_BOX)
	{
		cpPolyShape *poly = (cpPolyShape *)shape;
		cpVect q[] = { poly->planes[0].v0, poly->planes[1].v0, poly->planes[2].v0, poly->planes[3].v0 };
		(shape->userData ? srfPoison : srfFood).DrawQuad(q[0], q[1], q[2], q[3], *color);
		boxesOnScreen++;
		if (shape->body->p.y < - 100) cpSpaceAddPostStepCallback(space, (cpPostStepFunc)PostStepRemoveBody, shape->body, NULL);
	}
	if (shape->type == COLLISION_MONSTER)
	{
		cpPolyShape *poly = (cpPolyShape *)shape;
		cpVect q[] = { poly->planes[0].v0, poly->planes[1].v0, poly->planes[2].v0, poly->planes[3].v0 };
		(tickLastEat && ZLSINCE(tickLastEat) < 800 && ((ZLSINCE(tickLastEat)/100) & 1) ? srfEat : srfMonster).DrawQuad(q[0], q[1], q[2], q[3], *color);
	}
	if (shape->type == COLLISION_BELT)
	{
		ZL_Vector a = ((cpSegmentShape*)shape)->ta, b = ((cpSegmentShape*)shape)->tb;
		ZL_Vector p = ZL_Vector(a, b).VecNorm().Mul(10.f).VecPerp();
		if (((cpSegmentShape*)shape)->n.y > 0)
			srfBelt[(ZLTICKS/100)%3].DrawQuad(a.x + p.x, a.y + p.y, b.x + p.x, b.y + p.y, b.x - p.x, b.y - p.y, a.x - p.x, a.y - p.y, *color);
		else
			srfBelt[(ZLTICKS/100)%3].DrawQuad(a.x - p.x, a.y - p.y, b.x - p.x, b.y - p.y, b.x + p.x, b.y + p.y, a.x + p.x, a.y + p.y, *color);
	}
	if (shape->type == COLLISION_LEVER)
	{
		ZL_Vector a = ((cpSegmentShape*)shape)->ta, b = ((cpSegmentShape*)shape)->tb;
		ZL_Vector p = ZL_Vector(a, b).VecNorm().Mul(10.f).VecPerp();
		srfLever.DrawQuad(a.x + p.x, a.y + p.y, b.x + p.x, b.y + p.y, b.x - p.x, b.y - p.y, a.x - p.x, a.y - p.y, *color);
	}
	if (shape->type == COLLISION_BUMPER)
	{
		srfBumper.Draw(shape->body->p);
	}
	if (shape->type == COLLISION_WALL)
	{
		ZL_Vector a = ((cpSegmentShape*)shape)->ta, b = ((cpSegmentShape*)shape)->tb;
		ZL_Vector p = ZL_Vector(a, b).VecNorm().Mul(10.f).VecPerp();
		srfWall.DrawQuad(a.x + p.x, a.y + p.y, b.x + p.x, b.y + p.y, b.x - p.x, b.y - p.y, a.x - p.x, a.y - p.y, *color);
	}
}

#ifdef ZILLALOG
static void ExportThing(cpShape *shape, void *data)
{
	if (shape->type == COLLISION_BELT) printf("MakeBelt(cpv(%ff, %ff), %ff, %s);\n", shape->body->p.x, shape->body->p.y, shape->body->a, (shape->userData ? "true" : "false"));
	if (shape->type == COLLISION_WALL) printf("MakeWall(cpv(%ff, %ff), %ff);\n", shape->body->p.x, shape->body->p.y, shape->body->a);
	if (shape->type == COLLISION_LEVER) printf("MakeLever(cpv(%ff, %ff), %s);\n", shape->body->p.x, shape->body->p.y, (shape->body->a > CP_PI/4*2 ? "false" : "true"));
	if (shape->type == COLLISION_BUMPER) printf("MakeBumper(cpv(%ff, %ff));\n", shape->body->p.x, shape->body->p.y);
	if (shape->type == COLLISION_MONSTER) printf("MakeMonster(cpv(%ff, %ff));\n", shape->body->p.x, shape->body->p.y);
}
#endif

static void Draw()
{
	if (mode == MODE_PLAY)
	{
		cpVect mousePos = cpv(ZL_Display::PointerX - ZLHALFW, ZL_Display::PointerY);

		#ifdef ZILLALOG //MAP EDIT
		if (ZL_Input::Down(ZLK_SPACE))
		{
			StartLevel(stage + 1);
		}
		if (ZL_Input::Down(ZLK_F)) SpawnBox(mousePos);
		if (ZL_Input::Down(ZLK_1)) MakeLever(mousePos, false);
		if (ZL_Input::Down(ZLK_2)) MakeBelt(mousePos, 0, false);
		if (ZL_Input::Down(ZLK_3)) MakeWall(mousePos, 0);
		if (ZL_Input::Down(ZLK_4)) MakeBumper(mousePos);
		if (ZL_Input::Down(ZLK_M)) MakeMonster(mousePos);
		if (ZL_Input::Down(ZLK_S)) spawns.push_back(mousePos);
		if (ZL_Input::Held(ZLK_D))
		{
			static cpBody* dragBody;
			static cpVect lastMousePos;
			if (ZL_Input::Down(ZLK_D))
			{
				cpShape *shape = cpSpacePointQueryNearest(space, mousePos, 10, CP_SHAPE_FILTER_ALL, NULL);
				dragBody = (shape && shape->body && shape->body != space->staticBody ? shape->body : NULL);
			}
			else if (dragBody)
			{
				cpShape* dragShape = dragBody->shapeList;
				cpVect mouseDelta = cpvsub(mousePos, lastMousePos);
				cpBodySetPosition(dragBody, cpvadd(dragBody->p, mouseDelta));
				cpBodySetAngle(dragBody, dragBody->a + ZL_Math::Sign0(ZL_Input::MouseWheel()) * CP_PI / 8.f);
				cpSpaceReindexShapesForBody(space, dragBody);
				CP_BODY_FOREACH_CONSTRAINT(dragBody, dragConstraint)
					if (dragConstraint->b != space->staticBody)
						cpBodySetPosition(dragConstraint->b, cpvadd(dragConstraint->b->p, mouseDelta));
					else if (cpConstraintIsPivotJoint(dragConstraint))
						((cpPivotJoint*)dragConstraint)->anchorB = cpvadd(((cpPivotJoint*)dragConstraint)->anchorB, mouseDelta);
			}
			lastMousePos = mousePos;
		}
		if (ZL_Input::Held(ZLK_R))
		{
			cpShape *shape = cpSpacePointQueryNearest(space, mousePos, 10, CP_SHAPE_FILTER_ALL, NULL);
			if (shape && shape->body && shape->body != space->staticBody)
			{
				while (shape->body->constraintList)
				{
					if (shape->body->constraintList->b != space->staticBody)
						PostStepRemoveBody(space, shape->body->constraintList->b, NULL);
					cpSpaceRemoveConstraint(space, shape->body->constraintList);
				}
				PostStepRemoveBody(space, shape->body, NULL);
			}
		}
		if (ZL_Input::Down(ZLK_E))
		{
			printf("------------------------------------------------------------\n");
			for (cpVect v : spawns) printf("spawns.push_back(cpv(%ff, %ff));\n", v.x, v.y);
			cpSpaceEachShape(space, ExportThing, NULL);
			printf("------------------------------------------------------------\n");
		}
		#endif

		if (ZL_Input::Down())
		{
			cpPointQueryInfo info = {0};
			cpShape *shape = cpSpacePointQueryNearest(space, mousePos, 120.f, GRABBABLE_FILTER, &info);
			if(shape && cpBodyGetMass(cpShapeGetBody(shape)) < INFINITY)
			{
				cpVect nearest = (info.distance > 0.0f ? info.point : mousePos);
				cpBody *body = cpShapeGetBody(shape);
				mouseJoint = cpPivotJointNew2(mouseBody, body, cpvzero, cpBodyWorldToLocal(body, nearest));
				mouseJoint->maxForce = 5000000.0f;
				mouseJoint->errorBias = cpfpow(1.0f - 0.15f, 60.0f);
				cpSpaceAddConstraint(space, mouseJoint);
			}
			else if (!shape) 
			{
				shape = cpSpacePointQueryNearest(space, mousePos, 50.f, NOT_GRABBABLE_FILTER, &info);
				if (shape && shape->type == COLLISION_BELT)
				{
					sndToggle.Play();
					cpSegmentShape* beltShape = (cpSegmentShape*)shape;
					std::swap(beltShape->a, beltShape->b);
					std::swap(beltShape->ta, beltShape->tb);
					beltShape->n = cpvneg(beltShape->n);
					shape->userData = (cpDataPointer)(((size_t)shape->userData)^1);
				}
			}
		}
		if (ZL_Input::Up() && mouseJoint)
		{
			cpSpaceRemoveConstraint(space, mouseJoint);
			cpConstraintFree(mouseJoint);
			mouseJoint = NULL;
		}

		mouseBody->v = cpvmult(cpvsub(mousePos, mouseBody->p), 60.0f);
		mouseBody->p = mousePos;
	}

	if (mode == MODE_PLAY || mode == MODE_TITLE)
	{
		while (ZLSINCE(tickNextSpawn) > 0)
		{
			if (!spawns.empty())
			{
				SpawnBox(RAND_VECTORELEMENT(spawns)); 
			}
			tickNextSpawn += 2500;
		}

		static ticks_t TICKSUM = 0;
		for (TICKSUM += ZLELAPSEDTICKS; TICKSUM > 16; TICKSUM -= 16)
		{
			cpSpaceStep(space, s(16.0/1000.0));

			if (mode == MODE_PLAY)
			{
				if (foodNeed <= 0)
				{
					sndClear.Play();
					mode = MODE_CLEAR;
					modeTick = ZLTICKS;
					break;
				}
				if (foodNeed > 0 && foodLeft <= 0 && boxesOnScreen <= 0)
				{
					sndGameOver.Play();
					mode = MODE_GAMEOVER;
					modeTick = ZLTICKS;
					break;
				}
			}
		}
	}

	ZL_Display::FillGradient(0, 0, ZLWIDTH, ZLHEIGHT, bg[0], bg[1], bg[2], bg[3]);

	ZL_Display::PushMatrix();
	ZL_Display::Translate(ZLHALFW, 0);

	for (cpVect v : spawns)
		ZL_Display::FillTriangle(v.x, v.y, v.x + 50, v.y + 50, v.x - 50, v.y + 50, ZLRGBA(1,.8,.5,.5));

	ZL_Display::Translate(3, -3);
	cpSpaceEachShape(space, (cpSpaceShapeIteratorFunc)DrawThing, &colShadow);
	ZL_Display::Translate(-3, 3);
	boxesOnScreen = 0;
	cpSpaceEachShape(space, (cpSpaceShapeIteratorFunc)DrawThing, (void*)&ZL_Color::White);

	#ifdef ZILLALOG //DEBUG DRAW
	if (ZL_Display::KeyDown[ZLK_LSHIFT])
	{
		void DebugDrawShape(cpShape*,void*); cpSpaceEachShape(space, DebugDrawShape, NULL);
		void DebugDrawConstraint(cpConstraint*, void*); cpSpaceEachConstraint(space, DebugDrawConstraint, NULL);
	}
	#endif

	ZL_Display::PopMatrix();

	if (mode != MODE_TITLE && mode != MODE_FINISH)
	{
		DrawTextShadowed(txtFoodNeedX, ZLV(10, ZLFROMH(25)), .5f);
		DrawTextShadowed(txtFoodLeftX, ZLV(10, ZLFROMH(50)), .5f);

		if (stage == 1)
		{
			static ZL_TextBuffer txtHintGoal(fntMain, "Goal");
			static ZL_TextBuffer txtHintFeed(fntMain, "Feed It!");
			static ZL_TextBuffer txtHintDrag(fntMain, "Drag with mouse!");
			static ZL_TextBuffer txtHintCatch(fntMain, "Catch bananas!");
			static ZL_TextBuffer txtHintPoison(fntMain, "Avoid poison!");

			DrawTextShadowed(txtHintGoal, ZLV(150, ZLFROMH(80)), .5f, ZLLUMA(1, .5), ZLLUMA(0, .25));
			DrawTextShadowed(txtHintFeed, ZLV(ZLHALFW-460, 70), .5f, ZLLUMA(1, .5), ZLLUMA(0, .25));
			DrawTextShadowed(txtHintDrag, ZLV(ZLHALFW-400, 365), .5f, ZLLUMA(1, .5), ZLLUMA(0, .25));
			DrawTextShadowed(txtHintCatch, ZLV(ZLHALFW-60, ZLFROMH(30)), .5f, ZLLUMA(1, .5), ZLLUMA(0, .25));
			DrawTextShadowed(txtHintPoison, ZLV(ZLHALFW-60, ZLFROMH(60)), .5f, ZLLUMA(1, .5), ZLLUMA(0, .25));
		}
		else if (stage == 2)
		{
			static ZL_TextBuffer txtHintFling(fntMain, "Fling It!");

			DrawTextShadowed(txtHintFling, ZLV(ZLHALFW-400, 440), .5f, ZLLUMA(1, .5), ZLLUMA(0, .25));
		}
		else if (stage == 3)
		{
			static ZL_TextBuffer txtHintBelt(fntMain, "Click to change direction!");

			DrawTextShadowed(txtHintBelt, ZLV(ZLHALFW+180, 200), .5f, ZLLUMA(1, .5), ZLLUMA(0, .25));
		}
	}

	if (mode == MODE_TITLE)
	{
		static ZL_TextBuffer txtClickToPlay(fntMain, "Click to start");
		static ZL_TextBuffer txtFooter(fntMain, "(C) 2020 Bernhard Schelling");

		DrawTextBordered(txtClickToPlay, ZLV(ZLHALFW + 300, 160), .5f);
		DrawTextBordered(txtFooter, ZLV(ZLHALFW, 18), .4f);

		if (ZL_Input::Down() || ZL_Input::Down(ZLK_SPACE)) StartLevel(1);
		#ifndef __WEBAPP__
		else if (ZL_Input::Down(ZLK_ESCAPE, true)) { ZL_Application::Quit(); }
		#endif
	}
	else if (mode == MODE_PLAY)
	{
		if (ZLSINCE(modeTick) < 2000)
		{
			ZL_Color clearInner = ZLRGBA(1,1,1, 1-ZLSINCE(modeTick)/2000.f), clearOuter = ZLRGBA(0,0,0, 1-ZLSINCE(modeTick)/2000.f);
			DrawTextBordered(txtStageX, ZLV(ZLHALFW, ZLFROMH(100)), 1.f + ZLSINCE(modeTick)/2000.f, clearInner, clearOuter, 3);
		}

		if (ZL_Input::Down(ZLK_ESCAPE, true)) { mode = MODE_PAUSE; }
	}
	else if (mode == MODE_PAUSE)
	{
		static ZL_TextBuffer txtPaused(fntMain, "Paused");
		static ZL_TextBuffer txtResume(fntMain, "Press ESC or click to resume playing");
		static ZL_TextBuffer txtTitle(fntMain, "Press Q to go to title screen");
		static ZL_TextBuffer txtRestart(fntMain, "Press R to restart the stage");

		ZL_Display::FillRect(0, 0, ZLWIDTH, ZLHEIGHT, ZLLUMA(0, .5f));
		DrawTextBordered(txtPaused,  ZLV(ZLHALFW, ZLHALFH + 200), 2);
		DrawTextBordered(txtResume,  ZLV(ZLHALFW, ZLHALFH - 100));
		DrawTextBordered(txtTitle,   ZLV(ZLHALFW, ZLHALFH - 160));
		DrawTextBordered(txtRestart, ZLV(ZLHALFW, ZLHALFH - 220));

		if      (ZL_Input::Down(ZLK_ESCAPE) || ZL_Input::Down() || ZL_Input::Down(ZLK_SPACE)) mode = MODE_PLAY;
		else if (ZL_Input::Down(ZLK_Q)) StartLevel(0);
		else if (ZL_Input::Down(ZLK_R)) StartLevel(stage);
	}
	else if (mode == MODE_GAMEOVER)
	{
		static ZL_TextBuffer txtGameOver(fntMain, "Game Over!");
		static ZL_TextBuffer txtTryAgain(fntMain, "Click to try again");

		ZL_Display::FillRect(0, 0, ZLWIDTH, ZLHEIGHT, ZLLUMA(0, .5f*ZL_Math::Clamp01(ZLSINCE(modeTick)/1000.f)));
		DrawTextBordered(txtGameOver, ZL_Display::Center() + RAND_ANGLEVEC * RAND_RANGE(3,7) * ssin(ZLSINCE(modeTick)/200.f), 2);
		if (ZLSINCE(modeTick) > 350) DrawTextBordered(txtTryAgain, ZLV(ZLHALFW, ZLHALFH - 100), .75f);
		if (ZLSINCE(modeTick) > 500 && (ZL_Input::Down() || ZL_Input::Down(ZLK_SPACE))) StartLevel(stage);
	}
	else if (mode == MODE_CLEAR)
	{
		static ZL_TextBuffer txtClear(fntMain, "Clear!");

		ZL_Color clearInner = ZLRGBA(1,1,0, 1-ZLSINCE(modeTick)/2000.f), clearOuter = ZLRGBA(0,0,0, 1-ZLSINCE(modeTick)/2000.f);
		DrawTextBordered(txtClear, ZL_Display::Center(), 2.f + ZLSINCE(modeTick)/2000.f*2.f, clearInner, clearOuter, 3);
		if (ZLSINCE(modeTick) > 2000) StartLevel(stage + 1);
	}
	else if (mode == MODE_FINISH)
	{
		static ZL_TextBuffer txtCleared(fntMain, "Game Cleared!");
		static ZL_TextBuffer txtThanks(fntMain, "Thank you for playing!");
		static ZL_TextBuffer txtPlayAgain(fntMain, "Click to play again");

		DrawTextBordered(txtCleared,  ZLV(ZLHALFW, ZLHALFH + 200), 2);
		DrawTextBordered(txtThanks,  ZLV(ZLHALFW, ZLHALFH - 100), 2);

		if (ZLSINCE(modeTick) > 350) DrawTextBordered(txtPlayAgain, ZLV(ZLHALFW, ZLHALFH - 300));
		if (ZLSINCE(modeTick) > 500 && (ZL_Input::Down() || ZL_Input::Down(ZLK_SPACE))) StartLevel(1);
	}
}

static struct sFeedIt : public ZL_Application
{
	sFeedIt() : ZL_Application(60) { }

	virtual void Load(int argc, char *argv[])
	{
		if (!ZL_Application::LoadReleaseDesktopDataBundle()) return;
		if (!ZL_Display::Init("Feed It!", 1280, 720, ZL_DISPLAY_ALLOWRESIZEHORIZONTAL)) return;
		ZL_Display::ClearFill(ZL_Color::White);
		ZL_Display::SetAA(true);
		ZL_Audio::Init();
		ZL_Input::Init();
		Init();
	}

	virtual void AfterFrame()
	{
		Draw();
	}
} FeedIt;

#ifdef ZILLALOG //DEBUG DRAW
void DebugDrawShape(cpShape *shape, void*)
{
	switch (shape->klass->type)
	{
		case CP_CIRCLE_SHAPE: {
			cpCircleShape *circle = (cpCircleShape *)shape;
			ZL_Display::DrawCircle(circle->tc, circle->r, ZL_Color::Green);
			break; }
		case CP_SEGMENT_SHAPE: {
			cpSegmentShape *seg = (cpSegmentShape *)shape;
			cpVect vw = cpvclamp(cpvperp(cpvsub(seg->tb, seg->ta)), seg->r);
			//ZL_Display::DrawLine(seg->ta, seg->tb, ZLWHITE);
			ZL_Display::DrawQuad(seg->ta.x + vw.x, seg->ta.y + vw.y, seg->tb.x + vw.x, seg->tb.y + vw.y, seg->tb.x - vw.x, seg->tb.y - vw.y, seg->ta.x - vw.x, seg->ta.y - vw.y, ZLRGBA(0,1,1,.35), ZLRGBA(1,1,0,.35));
			ZL_Display::DrawCircle(seg->ta, seg->r, ZLRGBA(0,1,1,.35), ZLRGBA(1,1,0,.35));
			ZL_Display::DrawCircle(seg->tb, seg->r, ZLRGBA(0,1,1,.35), ZLRGBA(1,1,0,.35));
			break; }
		case CP_POLY_SHAPE: {
			cpPolyShape *poly = (cpPolyShape *)shape;
			{for (int i = 1; i < poly->count; i++) ZL_Display::DrawLine(poly->planes[i-1].v0, poly->planes[i].v0, ZLWHITE);}
			ZL_Display::DrawLine(poly->planes[poly->count-1].v0, poly->planes[0].v0, ZLWHITE);
			break; }
	}
	ZL_Display::FillCircle(cpBodyGetPosition(shape->body), 3, ZL_Color::Red);
	ZL_Display::DrawLine(cpBodyGetPosition(shape->body), (ZL_Vector&)cpBodyGetPosition(shape->body) + ZLV(cpBodyGetAngularVelocity(shape->body)*-10, 0), ZLRGB(1,0,0));
	ZL_Display::DrawLine(cpBodyGetPosition(shape->body), (ZL_Vector&)cpBodyGetPosition(shape->body) + ZL_Vector::FromAngle(cpBodyGetAngle(shape->body))*10, ZLRGB(1,1,0));
}

void DebugDrawConstraint(cpConstraint *constraint, void *data)
{
	cpBody *body_a = constraint->a, *body_b = constraint->b;

	if(cpConstraintIsPinJoint(constraint))
	{
		cpPinJoint *joint = (cpPinJoint *)constraint;
		cpVect a = (cpBodyGetType(body_a) == CP_BODY_TYPE_KINEMATIC ? body_a->p : cpTransformPoint(body_a->transform, joint->anchorA));
		cpVect b = (cpBodyGetType(body_b) == CP_BODY_TYPE_KINEMATIC ? body_b->p : cpTransformPoint(body_b->transform, joint->anchorB));
		ZL_Display::DrawLine(a.x, a.y, b.x, b.y, ZL_Color::Magenta);
	}
	else if (cpConstraintIsPivotJoint(constraint))
	{
		cpPivotJoint *joint = (cpPivotJoint *)constraint;
		cpVect a = (cpBodyGetType(body_a) == CP_BODY_TYPE_KINEMATIC ? body_a->p : cpTransformPoint(body_a->transform, joint->anchorA));
		cpVect b = (cpBodyGetType(body_b) == CP_BODY_TYPE_KINEMATIC ? body_b->p : cpTransformPoint(body_b->transform, joint->anchorB));
		ZL_Display::DrawLine(a.x, a.y, b.x, b.y, ZL_Color::Magenta);
	}
	else if (cpConstraintIsRotaryLimitJoint(constraint))
	{
		cpRotaryLimitJoint *joint = (cpRotaryLimitJoint *)constraint;
		cpVect a = cpTransformPoint(body_a->transform, cpvzero);
		cpVect b = cpvadd(a, cpvmult(cpvforangle(joint->min), 40));
		cpVect c = cpvadd(a, cpvmult(cpvforangle(joint->max), 40));
		ZL_Display::DrawLine(a.x, a.y, b.x, b.y, ZL_Color::Magenta);
		ZL_Display::DrawLine(a.x, a.y, c.x, c.y, ZL_Color::Magenta);
	}
}
#endif

// SOUND / MUSIC DATA

static const unsigned int IMCMUSIC_OrderTable[] = {
	0x011000001, 0x010000002, 0x012000001, 0x010100003, 0x011000001, 0x010000002, 0x012000000, 0x010100004,
	0x011000005, 0x010100000, 0x012000004, 0x010100006, 0x011000000, 0x010100004, 0x012000005, 0x010000000,
	0x023000007,
};
static const unsigned char IMCMUSIC_PatternData[] = {
	0x42, 0, 0x44, 0, 0x47, 0, 0, 0, 0x49, 0, 0x50, 0, 0, 0x50, 0, 0,
	0x49, 0, 0x54, 0, 0x52, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x50, 0, 0x57, 0, 0x54, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x60, 0, 0, 0, 0, 0, 0, 0, 0x59, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0x5B, 0, 0x57, 0, 0x62, 0, 0x60, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0x5B, 0, 0x64, 0, 0x64, 0, 0x60, 0,
	255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x50, 0, 0x50, 0, 0x52, 0,
	0x45, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x42, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0,
	0x50, 0, 0x54, 0, 0x50, 0, 0x54, 0, 0x50, 0, 0, 0, 0x50, 0, 0x54, 0,
	0x57, 0x57, 0x57, 0, 0x54, 0x54, 0x54, 0, 0x52, 0x52, 0x52, 0, 0x50, 0, 0, 0,
};
static const unsigned char IMCMUSIC_PatternLookupTable[] = { 0, 7, 7, 7, 7, 7, 8, 11, };
static const TImcSongEnvelope IMCMUSIC_EnvList[] = {
	{ 0, 256, 64, 8, 16, 255, true, 255, },
	{ 0, 256, 523, 8, 16, 255, true, 255, },
	{ 32, 256, 196, 8, 16, 255, true, 255, },
	{ 0, 256, 280, 1, 22, 6, true, 255, },
	{ 50, 100, 15, 8, 255, 255, false, 0, },
	{ 0, 386, 42, 8, 16, 255, true, 255, },
	{ 0, 256, 64, 8, 16, 255, true, 255, },
	{ 128, 256, 209, 8, 16, 255, true, 255, },
	{ 0, 128, 1046, 8, 16, 255, true, 255, },
	{ 0, 256, 261, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCMUSIC_EnvCounterList[] = {
	{ 0, 0, 256 }, { -1, -1, 256 }, { 1, 5, 256 }, { 2, 5, 256 },
	{ 3, 6, 177 }, { 4, 6, 100 }, { 5, 7, 386 }, { 6, 7, 256 },
	{ 7, 7, 256 }, { 8, 7, 128 }, { 9, 7, 256 },
};
static const TImcSongOscillator IMCMUSIC_OscillatorList[] = {
	{ 9, 0, IMCSONGOSCTYPE_SINE, 0, -1, 144, 1, 1 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, 0, 160, 1, 1 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 66, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 24, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 88, 0, 0 },
	{ 10, 0, IMCSONGOSCTYPE_SQUARE, 4, -1, 62, 0, 0 },
	{ 9, 0, IMCSONGOSCTYPE_SQUARE, 4, -1, 34, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, 3, 36, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 4, 5, 14, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 5, -1, 127, 1, 3 },
	{ 6, 0, IMCSONGOSCTYPE_SINE, 6, -1, 216, 1, 1 },
	{ 7, 0, IMCSONGOSCTYPE_SINE, 6, -1, 60, 1, 1 },
	{ 5, 150, IMCSONGOSCTYPE_SINE, 7, -1, 255, 7, 8 },
	{ 9, 0, IMCSONGOSCTYPE_NOISE, 7, -1, 255, 9, 1 },
	{ 5, 31, IMCSONGOSCTYPE_SINE, 7, -1, 255, 10, 1 },
};
static const TImcSongEffect IMCMUSIC_EffectList[] = {
	{ 136, 0, 29400, 0, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 122, 0, 7350, 5, IMCSONGEFFECTTYPE_DELAY, 0, 0 },
	{ 255, 156, 1, 5, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
	{ 227, 0, 1, 5, IMCSONGEFFECTTYPE_HIGHPASS, 1, 0 },
	{ 8382, 977, 1, 6, IMCSONGEFFECTTYPE_OVERDRIVE, 0, 1 },
	{ 0, 0, 101, 6, IMCSONGEFFECTTYPE_FLANGE, 5, 0 },
	{ 16, 173, 1, 6, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
	{ 234, 0, 1, 7, IMCSONGEFFECTTYPE_LOWPASS, 1, 0 },
	{ 220, 168, 1, 7, IMCSONGEFFECTTYPE_RESONANCE, 1, 1 },
};
static unsigned char IMCMUSIC_ChannelVol[8] = { 97, 0, 0, 0, 0, 192, 94, 102 };
static const unsigned char IMCMUSIC_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 2, 4, 6 };
static const bool IMCMUSIC_ChannelStopNote[8] = { true, false, false, false, true, true, false, true };
TImcSongData imcDataIMCMUSIC = {
	/*LEN*/ 0x11, /*ROWLENSAMPLES*/ 7350, /*ENVLISTSIZE*/ 10, /*ENVCOUNTERLISTSIZE*/ 11, /*OSCLISTSIZE*/ 16, /*EFFECTLISTSIZE*/ 9, /*VOL*/ 100,
	IMCMUSIC_OrderTable, IMCMUSIC_PatternData, IMCMUSIC_PatternLookupTable, IMCMUSIC_EnvList, IMCMUSIC_EnvCounterList, IMCMUSIC_OscillatorList, IMCMUSIC_EffectList,
	IMCMUSIC_ChannelVol, IMCMUSIC_ChannelEnvCounter, IMCMUSIC_ChannelStopNote };
ZL_SynthImcTrack imcMusic(&imcDataIMCMUSIC);


static const unsigned int IMCHIT_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCHIT_PatternData[] = {
	0x49, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCHIT_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCHIT_EnvList[] = {
	{ 0, 386, 65, 8, 16, 255, true, 255, },
	{ 0, 256, 174, 8, 16, 255, true, 255, },
	{ 128, 256, 173, 8, 16, 255, true, 255, },
	{ 0, 128, 2615, 8, 16, 255, true, 255, },
	{ 0, 256, 348, 5, 19, 255, true, 255, },
	{ 0, 256, 418, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCHIT_EnvCounterList[] = {
	{ 0, 0, 386 }, { 1, 0, 256 }, { 2, 0, 256 }, { 3, 0, 128 },
	{ -1, -1, 258 }, { 4, 0, 238 }, { -1, -1, 256 }, { 5, 0, 256 },
};
static const TImcSongOscillator IMCHIT_OscillatorList[] = {
	{ 5, 150, IMCSONGOSCTYPE_SINE, 0, -1, 255, 1, 2 },
	{ 9, 15, IMCSONGOSCTYPE_NOISE, 0, -1, 255, 3, 4 },
	{ 5, 200, IMCSONGOSCTYPE_SINE, 0, -1, 170, 5, 6 },
	{ 5, 174, IMCSONGOSCTYPE_SINE, 0, -1, 230, 7, 6 },
};
static const TImcSongEffect IMCHIT_EffectList[] = {
	{ 113, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 6, 0 },
	{ 220, 168, 1, 0, IMCSONGEFFECTTYPE_RESONANCE, 6, 6 },
};
static unsigned char IMCHIT_ChannelVol[8] = { 148, 128, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCHIT_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCHIT_ChannelStopNote[8] = { true, true, false, false, false, false, false, false };
TImcSongData imcDataIMCHIT = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 5512, /*ENVLISTSIZE*/ 6, /*ENVCOUNTERLISTSIZE*/ 8, /*OSCLISTSIZE*/ 4, /*EFFECTLISTSIZE*/ 2, /*VOL*/ 100,
	IMCHIT_OrderTable, IMCHIT_PatternData, IMCHIT_PatternLookupTable, IMCHIT_EnvList, IMCHIT_EnvCounterList, IMCHIT_OscillatorList, IMCHIT_EffectList,
	IMCHIT_ChannelVol, IMCHIT_ChannelEnvCounter, IMCHIT_ChannelStopNote };

static const unsigned int IMCEAT_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCEAT_PatternData[] = {
	0x50, 0, 0, 0, 0x47, 0, 0, 0, 0x45, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCEAT_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCEAT_EnvList[] = {
	{ 0, 256, 65, 8, 16, 4, true, 255, },
	{ 0, 256, 53, 8, 16, 255, true, 255, },
	{ 0, 256, 81, 3, 21, 255, true, 255, },
	{ 0, 256, 53, 8, 16, 255, true, 255, },
	{ 0, 256, 64, 1, 22, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCEAT_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 206 }, { -1, -1, 256 },
	{ 3, 0, 256 }, { 4, 0, 158 },
};
static const TImcSongOscillator IMCEAT_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 124, 1, 2 },
	{ 6, 0, IMCSONGOSCTYPE_SQUARE, 0, -1, 62, 4, 5 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 50, 3, 3 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static unsigned char IMCEAT_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCEAT_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCEAT_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCEAT = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 4410, /*ENVLISTSIZE*/ 5, /*ENVCOUNTERLISTSIZE*/ 6, /*OSCLISTSIZE*/ 10, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 100,
	IMCEAT_OrderTable, IMCEAT_PatternData, IMCEAT_PatternLookupTable, IMCEAT_EnvList, IMCEAT_EnvCounterList, IMCEAT_OscillatorList, NULL,
	IMCEAT_ChannelVol, IMCEAT_ChannelEnvCounter, IMCEAT_ChannelStopNote };

static const unsigned int IMCBOING_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCBOING_PatternData[] = {
	0x49, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCBOING_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCBOING_EnvList[] = {
	{ 0, 256, 69, 0, 24, 255, true, 255, },
	{ 0, 256, 27, 8, 255, 255, true, 255, },
	{ 100, 200, 444, 0, 255, 255, true, 255, },
	{ 0, 256, 277, 31, 25, 255, true, 255, },
	{ 0, 256, 65, 8, 15, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCBOING_EnvCounterList[] = {
	{ 0, 0, 128 }, { 1, 0, 256 }, { 2, 0, 150 }, { -1, -1, 256 },
	{ 3, 0, 98 }, { 4, 0, 256 },
};
static const TImcSongOscillator IMCBOING_OscillatorList[] = {
	{ 8, 200, IMCSONGOSCTYPE_SQUARE, 0, -1, 58, 1, 2 },
	{ 6, 106, IMCSONGOSCTYPE_SAW, 0, -1, 100, 4, 5 },
	{ 255, 150, IMCSONGOSCTYPE_SAW, 0, 0, 16, 3, 3 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 1, 56, 3, 3 },
};
static const TImcSongEffect IMCBOING_EffectList[] = {
	{ 110, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 3, 0 },
};
static unsigned char IMCBOING_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCBOING_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCBOING_ChannelStopNote[8] = { true, false, false, false, false, false, false, false };
TImcSongData imcDataIMCBOING = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 5, /*ENVCOUNTERLISTSIZE*/ 6, /*OSCLISTSIZE*/ 4, /*EFFECTLISTSIZE*/ 1, /*VOL*/ 100,
	IMCBOING_OrderTable, IMCBOING_PatternData, IMCBOING_PatternLookupTable, IMCBOING_EnvList, IMCBOING_EnvCounterList, IMCBOING_OscillatorList, IMCBOING_EffectList,
	IMCBOING_ChannelVol, IMCBOING_ChannelEnvCounter, IMCBOING_ChannelStopNote };

static const unsigned int IMCGAMEOVER_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCGAMEOVER_PatternData[] = {
	0x34, 0x34, 0x32, 0x32, 0x30, 0x30, 0x30, 0, 255, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCGAMEOVER_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCGAMEOVER_EnvList[] = {
	{ 0, 256, 65, 8, 16, 4, true, 255, },
	{ 0, 256, 28, 8, 255, 255, true, 255, },
	{ 100, 200, 199, 3, 53, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCGAMEOVER_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 180 },
};
static const TImcSongOscillator IMCGAMEOVER_OscillatorList[] = {
	{ 8, 200, IMCSONGOSCTYPE_SQUARE, 0, -1, 100, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static unsigned char IMCGAMEOVER_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCGAMEOVER_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCGAMEOVER_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCGAMEOVER = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 11025, /*ENVLISTSIZE*/ 3, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 7, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 100,
	IMCGAMEOVER_OrderTable, IMCGAMEOVER_PatternData, IMCGAMEOVER_PatternLookupTable, IMCGAMEOVER_EnvList, IMCGAMEOVER_EnvCounterList, IMCGAMEOVER_OscillatorList, NULL,
	IMCGAMEOVER_ChannelVol, IMCGAMEOVER_ChannelEnvCounter, IMCGAMEOVER_ChannelStopNote };

static const unsigned int IMCCLEAR_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCCLEAR_PatternData[] = {
	0x44, 0, 0x47, 0x47, 0x40, 0x44, 0x47, 0x42, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCCLEAR_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCCLEAR_EnvList[] = {
	{ 0, 256, 65, 8, 16, 4, true, 255, },
	{ 0, 256, 26, 8, 16, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCCLEAR_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { -1, -1, 256 },
};
static const TImcSongOscillator IMCCLEAR_OscillatorList[] = {
	{ 9, 66, IMCSONGOSCTYPE_SAW, 0, -1, 100, 1, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, 0, 4, 2, 2 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static const TImcSongEffect IMCCLEAR_EffectList[] = {
	{ 85, 0, 1, 0, IMCSONGEFFECTTYPE_LOWPASS, 2, 0 },
};
static unsigned char IMCCLEAR_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCCLEAR_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCCLEAR_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCCLEAR = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 3307, /*ENVLISTSIZE*/ 2, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 9, /*EFFECTLISTSIZE*/ 1, /*VOL*/ 100,
	IMCCLEAR_OrderTable, IMCCLEAR_PatternData, IMCCLEAR_PatternLookupTable, IMCCLEAR_EnvList, IMCCLEAR_EnvCounterList, IMCCLEAR_OscillatorList, IMCCLEAR_EffectList,
	IMCCLEAR_ChannelVol, IMCCLEAR_ChannelEnvCounter, IMCCLEAR_ChannelStopNote };

static const unsigned int IMCTOGGLE_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCTOGGLE_PatternData[] = {
	0x64, 0x65, 0x67, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCTOGGLE_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCTOGGLE_EnvList[] = {
	{ 0, 256, 65, 8, 16, 4, true, 255, },
	{ 0, 256, 370, 8, 12, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCTOGGLE_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { -1, -1, 256 },
};
static const TImcSongOscillator IMCTOGGLE_OscillatorList[] = {
	{ 9, 66, IMCSONGOSCTYPE_SQUARE, 0, -1, 126, 1, 2 },
	{ 7, 66, IMCSONGOSCTYPE_SAW, 0, 0, 242, 2, 2 },
};
static unsigned char IMCTOGGLE_ChannelVol[8] = { 51, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCTOGGLE_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCTOGGLE_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCTOGGLE = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 2594, /*ENVLISTSIZE*/ 2, /*ENVCOUNTERLISTSIZE*/ 3, /*OSCLISTSIZE*/ 2, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 100,
	IMCTOGGLE_OrderTable, IMCTOGGLE_PatternData, IMCTOGGLE_PatternLookupTable, IMCTOGGLE_EnvList, IMCTOGGLE_EnvCounterList, IMCTOGGLE_OscillatorList, NULL,
	IMCTOGGLE_ChannelVol, IMCTOGGLE_ChannelEnvCounter, IMCTOGGLE_ChannelStopNote };

static const unsigned int IMCPOISON_OrderTable[] = {
	0x000000001,
};
static const unsigned char IMCPOISON_PatternData[] = {
	0x39, 0, 0x37, 0, 0x35, 0, 0x34, 0, 0x32, 0, 0, 0, 0, 0, 0, 0,
};
static const unsigned char IMCPOISON_PatternLookupTable[] = { 0, 1, 1, 1, 1, 1, 1, 1, };
static const TImcSongEnvelope IMCPOISON_EnvList[] = {
	{ 0, 256, 65, 8, 16, 4, true, 255, },
	{ 0, 256, 53, 8, 16, 255, true, 255, },
	{ 0, 256, 81, 3, 21, 255, true, 255, },
	{ 0, 256, 53, 8, 16, 255, true, 255, },
	{ 0, 256, 64, 1, 22, 255, true, 255, },
};
static TImcSongEnvelopeCounter IMCPOISON_EnvCounterList[] = {
	{ 0, 0, 256 }, { 1, 0, 256 }, { 2, 0, 206 }, { -1, -1, 256 },
	{ 3, 0, 256 }, { 4, 0, 158 },
};
static const TImcSongOscillator IMCPOISON_OscillatorList[] = {
	{ 8, 0, IMCSONGOSCTYPE_SINE, 0, -1, 124, 1, 2 },
	{ 6, 0, IMCSONGOSCTYPE_SQUARE, 0, -1, 62, 4, 5 },
	{ 8, 0, IMCSONGOSCTYPE_NOISE, 0, 0, 50, 3, 3 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 1, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 2, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 3, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 4, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 5, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 6, -1, 100, 0, 0 },
	{ 8, 0, IMCSONGOSCTYPE_SINE, 7, -1, 100, 0, 0 },
};
static unsigned char IMCPOISON_ChannelVol[8] = { 100, 100, 100, 100, 100, 100, 100, 100 };
static const unsigned char IMCPOISON_ChannelEnvCounter[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static const bool IMCPOISON_ChannelStopNote[8] = { false, false, false, false, false, false, false, false };
TImcSongData imcDataIMCPOISON = {
	/*LEN*/ 0x1, /*ROWLENSAMPLES*/ 4410, /*ENVLISTSIZE*/ 5, /*ENVCOUNTERLISTSIZE*/ 6, /*OSCLISTSIZE*/ 10, /*EFFECTLISTSIZE*/ 0, /*VOL*/ 100,
	IMCPOISON_OrderTable, IMCPOISON_PatternData, IMCPOISON_PatternLookupTable, IMCPOISON_EnvList, IMCPOISON_EnvCounterList, IMCPOISON_OscillatorList, NULL,
	IMCPOISON_ChannelVol, IMCPOISON_ChannelEnvCounter, IMCPOISON_ChannelStopNote };
