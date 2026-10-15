#pragma once

void GameBoot();
void GameFrame(float dt);
int GameStuds();
int GameStudTotal();
void GameDebugSetPlayer(float x, float y, float z);
void GameDebugSetYaw(float yaw);
float GamePlayerX();
float GamePlayerY();
float GamePlayerZ();
bool GameGrounded();
const char* GameStatus();
int GameRunTests();
