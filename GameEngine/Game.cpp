﻿//
// Game.cpp
//

#include "pch.h"
#include "Game.h"
#include "ModelEffect.h"
#include <fstream>
#include <sstream>
#include <algorithm>

extern void ExitGame();

using namespace DirectX;
using namespace DirectX::SimpleMath;

using Microsoft::WRL::ComPtr;

//static variable
Game* Game::m_Instance;

Game * Game::GetInstance()
{
	assert(m_Instance);

	return m_Instance;
}

Game::Game()
{
	assert(m_Instance == nullptr);
	m_Instance = this;
	m_deviceResources = DX::DeviceResources::GetInstance();
    m_deviceResources->RegisterDeviceNotify(this);
}

Game::~Game()
{
	m_Instance = nullptr;
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{
    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    // TODO: Change the timer settings if you want something other than the default variable timestep mode.
    // e.g. for 60 FPS fixed timestep update logic, call:
    /*
    m_timer.SetFixedTimeStep(true);
    m_timer.SetTargetElapsedSeconds(1.0 / 60);
    */

	{
		// デバッグカメラを生成
		m_DebugCamera = std::make_unique<DebugCamera>(width, height);

		// 追従カメラを生成
		m_FollowCamera = std::make_unique<FollowCamera>(width, height);

		// ロックオンカメラを生成
		m_LockOnCamera = std::make_unique<LockOnCamera>(width, height);

		// ロックオンカメラを有効に
		m_CurrentCamera = m_LockOnCamera.get();
	}
		
	{// Obj3Dのシステム初期化
		Obj3D::CommonDef def;
		def.pDevice = m_deviceResources->GetD3DDevice();
		def.pDeviceContext = m_deviceResources->GetD3DDeviceContext();
		def.pCamera = m_CurrentCamera;

		Obj3D::InitializeCommon(def);
	}

	{// LandShapeの静的な初期化
		LandShapeCommon::Def def;
		def.pCamera = m_CurrentCamera;
		def.pDevice = m_deviceResources->GetD3DDevice();
		def.pDeviceContext = m_deviceResources->GetD3DDeviceContext();

		LandShape::InitializeCommon(def);
	}

	// プレイヤー作成
	m_Player = std::make_unique<Player>();

	m_Enemies.resize(ENEMY_NUM);
	for (int i = 0; i < ENEMY_NUM; i++)
	{
		m_Enemies[i] = std::make_unique<Enemy>();
		m_Enemies[i]->Initialize();
	}

	// ロックオンを生成
	m_LockOn = std::make_unique<LockOn>();
	m_LockOn->SetPlayer(m_Player.get());
	m_LockOn->SetEnemies(&m_Enemies);
	// ロックオンカメラにロックオンをセット
	m_LockOnCamera->SetLockOn(m_LockOn.get());
	
	// モデルをファイルからロード
	m_ObjSkydome = std::make_unique<Obj3D>();
	m_ObjSkydome->DisableLighting();

	{// 地形ロード
		wifstream ifs("Resources/CSV/LandShape.csv");

		wstring line;

		// 1行目をスキップ
		getline(ifs, line);

		// 各行のデータを解析
		while (getline(ifs, line)) {

			wstring filename_landshape;
			wstring filename_cmo;			

			// 1行分を文字列ストリームに変換
			wstringstream stream(line);

			// 区切り記号
			wchar_t delim = L',';

			// ファイル名の文字列を読み込み
			getline(stream, filename_landshape, delim);

			// ファイル名の文字列を読み込み
			getline(stream, filename_cmo, delim);

			std::unique_ptr<LandShape> landshape = std::make_unique<LandShape>();
			// 読み込む
			landshape->Initialize(filename_landshape.c_str(), filename_cmo.c_str());

			getline(stream, line);

			// カンマを空白に置き換え
			std::replace(line.begin(), line.end(), L',', L' ');

			// 残りを文字列ストリームに再変換
			stream.str(L"");
			stream.clear(wstringstream::goodbit);
			stream << line;

			// スケーリングの読み取り
			float scale;
			stream >> scale;
			landshape->SetScale(scale);

			// 角度の読み取り
			Vector3 rotation;
			stream >> rotation.z;
			stream >> rotation.x;
			stream >> rotation.y;
			rotation.x = XMConvertToRadians(rotation.x);
			rotation.y = XMConvertToRadians(rotation.y);
			rotation.z = XMConvertToRadians(rotation.z);
			landshape->SetRot(rotation);

			// 座標の読み取り
			Vector3 position;
			stream >> position.x;
			stream >> position.y;
			stream >> position.z;
			landshape->SetTrans(position);

			// ライト無効
			int light_cut;
			stream >> light_cut;

			if (light_cut)
			{
				landshape->DisableLighting();
			}

			// コンテナに追加
			m_pLandShapeArray.push_back(std::move(landshape));
		}
	}
	m_Player->SetLandShapeArray(&m_pLandShapeArray);

	m_ObjSkydome->LoadModel(L"skydome");

	m_debugText = std::make_unique<DebugText>(m_deviceResources->GetD3DDevice(), m_deviceResources->GetSpriteBatch());

	// 指定範囲をランダムで返すラムダ式
	auto rand_value = [](float min, float max)
	{
		return (max - min) * (float)rand() / RAND_MAX + min;
	};

	// 木の数
	const int TREE_NUM = 50;
	m_ObjTrees.resize(TREE_NUM);
	for (unsigned int i = 0; i < m_ObjTrees.size(); i++)
	{
		m_ObjTrees[i].LoadModel(L"billboardGrass");
		m_ObjTrees[i].EnableAlpha();
		m_ObjTrees[i].DisableLighting();
		m_ObjTrees[i].SetScale(Vector3(1,1,1));
		float x = rand_value(-5.0f, 5.0f);
		float z = rand_value(-5.0f, 5.0f);
		m_ObjTrees[i].SetTrans(Vector3(x, 0, z));
	}
}

// Executes the basic game loop.
void Game::Tick()
{
    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    Render();
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    float elapsedTime = float(timer.GetElapsedSeconds());

	float fps = (float)m_timer.GetFramesPerSecond();

	m_debugText->AddText(Vector2(0, 0), L"FPS:%d", m_timer.GetFramesPerSecond());

	for (unsigned int i = 0; i < m_ObjTrees.size(); i++)
	{
		m_ObjTrees[i].Update();
	}

	for (std::vector<std::unique_ptr<Enemy>>::iterator it = m_Enemies.begin();
		it != m_Enemies.end();
		it++)
	{
		(*it)->Update();
	}
	
	for (std::vector<std::unique_ptr<LandShape>>::iterator it = m_pLandShapeArray.begin();
		it != m_pLandShapeArray.end();
		it++)
	{
		(*it)->Update();
	}

	m_Player->Update();	

	for (std::vector<std::unique_ptr<HomingBullet>>::iterator it = m_HomingBullets.begin();
		it != m_HomingBullets.end();
		)
	{
		if ((*it)->Update())
		{
			it = m_HomingBullets.erase(it);
		}
		else
		{
			it++;
		}
	}

	// 攻撃当たり判定
	{
		// プレイヤーの攻撃当たり球
		const Sphere& sphereA = m_Player->GetCollisionNodeBullet();

		// 全ての敵について判定する
		for (std::vector<std::unique_ptr<Enemy>>::iterator it = m_Enemies.begin();
			it != m_Enemies.end();
			)
		{
			Enemy* enemy = it->get();

			// 敵の被攻撃当たり球
			const Sphere& sphereB = enemy->GetCollisionNodeBody();

			Vector3 inter;

			// 球と球の当たり
			if (CheckSphere2Sphere(sphereA, sphereB, &inter))
			{
				m_Player->ResetBullet();

				ModelEffectManager::getInstance()->Entry(
					L"HitEffect",
					10,
					inter,	// 座標
					Vector3(0, 0, 0),	// 速度
					Vector3(0, 0, 0),	// 加速度
					Vector3(0, 0, 0),	// 回転角（初期）
					Vector3(0, 0, 0),	// 回転角（最終）
					Vector3(0, 0, 0),	// スケール（初期）
					Vector3(6, 6, 6)	// スケール（最終）
				);
				it = m_Enemies.erase(it);
			}
			else
			{
				it++;
			}
		}
	}

	KeyboardUtil* key = DX::DeviceResources::GetInstance()->GetKeyboardUtil();
	key->Update();

	MouseUtil* mouse = DX::DeviceResources::GetInstance()->GetMouseUtil();
	mouse->Update();

	if (key->IsTriggered(Keyboard::D1))
	{
		CollisionNode::SetDebugVisible(!CollisionNode::GetDebugVisible());
	}

	// カメラ更新
	m_DebugCamera->Update();

	// 追従カメラ
	m_FollowCamera->SetTarget(m_Player.get());
	m_FollowCamera->Update();

	// ロックオンカメラ
	m_LockOnCamera->Update();

	{
		bool hit = false;

		XMINT2 mousepos = mouse->GetPos();
		Vector2 mousePos((float)mousepos.x, (float)mousepos.y);
		Segment segment;
		m_CurrentCamera->UnProject(mousePos, &segment);

		// 大きい数字で初期化
		float distance = 1.0e5;
		Vector3 inter;

		for (std::vector<std::unique_ptr<LandShape>>::iterator it = m_pLandShapeArray.begin();
			it != m_pLandShapeArray.end();
			it++)
		{
			LandShape* pLandShape = it->get();
			float temp_distance;
			Vector3 temp_inter;

			// 床面との当たりを判定
			if (pLandShape->IntersectSegment(segment, &temp_inter))
			{
				hit = true;
				temp_distance = Vector3::Distance(segment.start, temp_inter);
				if (temp_distance < distance)
				{
					inter = temp_inter;
					distance = temp_distance;
				}
			}
		}

		static int interval = 0;
		interval--;

		if (hit)
		{
			if (interval <= 0)
			{
				interval = 10;

				ModelEffectManager::getInstance()->Entry(
					L"HitEffect",
					10,
					inter,	// 座標
					Vector3(0, 0, 0),	// 速度
					Vector3(0, 0, 0),	// 加速度
					Vector3(0, 0, 0),	// 回転角（初期）
					Vector3(0, 70, 0),	// 回転角（最終）
					Vector3(0, 0, 0),	// スケール（初期）
					Vector3(3, 3, 3)	// スケール（最終）
				);
			}
		}
	}

	ModelEffectManager::getInstance()->Update();
}

// Draws the scene.
void Game::Render()
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    Clear();
    m_deviceResources->PIXBeginEvent(L"Render");

	SpriteBatch* spriteBatch = m_deviceResources->GetSpriteBatch();
	spriteBatch->Begin();
	
    // TODO: Add your rendering code here.
	m_ObjSkydome->Draw();
	for (std::vector<std::unique_ptr<LandShape>>::iterator it = m_pLandShapeArray.begin();
		it != m_pLandShapeArray.end();
		it++)
	{
		(*it)->Draw();
	}

	auto context = m_deviceResources->GetD3DDeviceContext();

	m_Player->Draw();

	for (std::vector<std::unique_ptr<Enemy>>::iterator it = m_Enemies.begin();
		it != m_Enemies.end();
		it++)
	{
		(*it)->Draw();
	}

	for (std::vector<std::unique_ptr<HomingBullet>>::iterator it = m_HomingBullets.begin();
		it != m_HomingBullets.end();
		it++)
	{
		(*it)->Draw();
	}

	for (unsigned int i = 0; i < m_ObjTrees.size(); i++)
	{
		if (i >= m_ObjTrees.size() / 2)
		{
			m_ObjTrees[i].DrawBillboard();
		}
		else
		{
			m_ObjTrees[i].DrawBillboardConstrainY();
		}
	}

	ModelEffectManager::getInstance()->Draw();
    m_deviceResources->PIXEndEvent();

	m_debugText->Draw();
	spriteBatch->End();
    // Show the new frame.
    m_deviceResources->Present();
}

// Helper method to clear the back buffers.
void Game::Clear()
{
	m_deviceResources->PIXBeginEvent(L"Clear");

	// Clear the views.
	auto context = m_deviceResources->GetD3DDeviceContext();
	auto renderTarget = m_deviceResources->GetRenderTargetView();
	auto depthStencil = m_deviceResources->GetDepthStencilView();

	context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
	context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	context->OMSetRenderTargets(1, &renderTarget, depthStencil);

	// Set the viewport.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);

	m_deviceResources->PIXEndEvent();
}
#pragma endregion

#pragma region Message Handlers
// Message handlers
void Game::OnActivated()
{
    // TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
    // TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();

    // TODO: Game is being power-resumed (or returning from minimize).
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();

    // TODO: Game window is being resized.
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const
{
    // TODO: Change to desired default window size (note minimum size is 320x200).
    width = 800;
    height = 600;
}
void Game::FireHomingBullets(const DirectX::SimpleMath::Vector3 pos)
{
	// 敵の数分だけ発射
	unsigned int enemyNum = m_Enemies.size();
	for (unsigned int i = 0; i < enemyNum; i++)
	{
		Enemy* enemy = m_Enemies[i].get();

		// 弾生成
		std::unique_ptr<HomingBullet> bullet = std::make_unique<HomingBullet>();
		bullet->Initialize();
		// 上に発射
		bullet->Fire(pos, Vector3::UnitY);
		bullet->SetTarget(enemy);

		m_HomingBullets.push_back(std::move(bullet));
	}
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    // TODO: Initialize device dependent objects here (independent of window size).
    device;
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
    // TODO: Initialize windows-size dependent objects here.
}

void Game::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();

    CreateWindowSizeDependentResources();
}
#pragma endregion
