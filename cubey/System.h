#pragma once

#include "Event.h"

namespace cubey {
	
	template<typename T>
	class System {
	public:
		virtual ~System() {}

		static T* Main() {
			static T instance;
			return &instance;
		}

		virtual void Init() {
			EventChannel<Engine::TerminationEvent>::Add([this](const Engine::TerminationEvent& e){
				Terminate();
			});
			EventChannel<Engine::EarlyUpdateEvent>::Add([this](const Engine::EarlyUpdateEvent& e){
				EarlyUpdate(e.deltatime);
			});
			EventChannel<Engine::UpdateEvent>::Add([this](const Engine::UpdateEvent& e){
				Update(e.deltatime);
			});
			EventChannel<Engine::LateUpdateEvent>::Add([this](const Engine::LateUpdateEvent& e){
				LateUpdate(e.deltatime);
			});
			EventChannel<Engine::RenderEvent>::Add([this](const Engine::RenderEvent& e){
				Render();
			});
			EventChannel<Engine::UIRenderEvent>::Add([this](const Engine::UIRenderEvent& e){
				UIRender();
			});
		}
		virtual void Terminate() {}

		virtual void EarlyUpdate(float delta_time) {}
		virtual void Update(float delta_time) {}
		virtual void LateUpdate(float delta_time) {}

		virtual void Render() {}
		virtual void UIRender() {}
	};
}


