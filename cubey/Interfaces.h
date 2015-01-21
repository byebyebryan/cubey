#pragma once

#include "Event.h"
#include "Engine.h"

namespace cubey {

	template<typename T>
	class ISingleton {
	public:
		virtual ~ISingleton() {}
		static T* Main() {
			static T* instance = new T();
			return instance;
		}
	protected:
		ISingleton() {}
	};

	class IEngineEvents {
	public:
		IEngineEvents();
		virtual ~IEngineEvents() {}

		virtual void Init();

	protected:
		virtual void StartUp() {}
		virtual void Terminate() {}

		virtual void EarlyUpdate(float delta_time) {}
		virtual void Update(float delta_time) {}
		virtual void LateUpdate(float delta_time) {}

		virtual void Render() {}
		virtual void UIRender() {}

	private:
		EventLisenter<Engine::StartUpEvent> start_up_lisenter_;
		EventLisenter<Engine::TerminateEvent> terminate_lisenter_;
		EventLisenter<Engine::EarlyUpdateEvent> early_update_lisenter_;
		EventLisenter<Engine::UpdateEvent> update_lisenter_;
		EventLisenter<Engine::LateUpdateEvent> late_update_lisenter_;
		EventLisenter<Engine::RenderEvent> render_lisenter_;
		EventLisenter<Engine::UIRenderEvent> ui_render_lisenter_;
	};
}


