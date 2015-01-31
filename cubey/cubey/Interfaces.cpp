#include "Interfaces.h"

namespace cubey {
	IEngineEvents::IEngineEvents() {
		system_init_lisenter_ = EventLisenter<Engine::SystemInitEvent>([this](const Engine::SystemInitEvent& e){
			SystemInit();
		});
		init_lisenter_ = EventLisenter<Engine::InitEvent>([this](const Engine::InitEvent& e){
			Init();
		});
		start_up_lisenter_ = EventLisenter<Engine::StartUpEvent>([this](const Engine::StartUpEvent& e){
			StartUp();
		});
		terminate_lisenter_ = EventLisenter<Engine::TerminateEvent>([this](const Engine::TerminateEvent& e){
			Terminate();
		});

		early_update_lisenter_ = EventLisenter<Engine::EarlyUpdateEvent>([this](const Engine::EarlyUpdateEvent& e){
			EarlyUpdate(e.deltatime);
		});
		update_lisenter_ = EventLisenter<Engine::UpdateEvent>([this](const Engine::UpdateEvent& e){
			Update(e.deltatime);
		});
		late_update_lisenter_ = EventLisenter<Engine::LateUpdateEvent>([this](const Engine::LateUpdateEvent& e){
			LateUpdate(e.deltatime);
		});
		render_lisenter_ = EventLisenter<Engine::RenderEvent>([this](const Engine::RenderEvent& e){
			Render();
		});
		ui_render_lisenter_ = EventLisenter<Engine::UIRenderEvent>([this](const Engine::UIRenderEvent& e){
			UIRender();
		});
		system_init_lisenter_.PushToChannel();
		init_lisenter_.PushToChannel();
		start_up_lisenter_.PushToChannel();
		terminate_lisenter_.PushToChannel();
		early_update_lisenter_.PushToChannel();
		update_lisenter_.PushToChannel();
		late_update_lisenter_.PushToChannel();
		render_lisenter_.PushToChannel();
		ui_render_lisenter_.PushToChannel();
	}

	
}