#pragma once

#include "Event.h"
#include "Engine.h"

namespace cubey {

	class EngineEventsBase {
	public:
		EngineEventsBase() {

			init_lisenter_ = EventLisenter<Engine::InitEvent>([this](const Engine::InitEvent& e){
				Init();
			});
			start_up_lisenter_ = EventLisenter<Engine::StartUpEvent>([this](const Engine::StartUpEvent& e){
				StartUp();
			});
			close_down_lisenter_ = EventLisenter<Engine::CloseDownEvent>([this](const Engine::CloseDownEvent& e){
				CloseDown();
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

			
		}
		virtual ~EngineEventsBase() {}

		void PushToEngine() {
			init_lisenter_.PushToChannel();
			start_up_lisenter_.PushToChannel();
			terminate_lisenter_.PushToChannel();
			early_update_lisenter_.PushToChannel();
			update_lisenter_.PushToChannel();
			late_update_lisenter_.PushToChannel();
			render_lisenter_.PushToChannel();
			ui_render_lisenter_.PushToChannel();
		}

		void PopFromEngine() {
			init_lisenter_.RemoveFromChannel();
			start_up_lisenter_.RemoveFromChannel();
			terminate_lisenter_.RemoveFromChannel();
			early_update_lisenter_.RemoveFromChannel();
			update_lisenter_.RemoveFromChannel();
			late_update_lisenter_.RemoveFromChannel();
			render_lisenter_.RemoveFromChannel();
			ui_render_lisenter_.RemoveFromChannel();
		}

		virtual void StartUp() {}
		virtual void CloseDown() {}

	protected:

		virtual void Init() {}

		virtual void EarlyUpdate(float delta_time) {}
		virtual void Update(float delta_time) {}
		virtual void LateUpdate(float delta_time) {}

		virtual void Render() {}
		virtual void UIRender() {}
		
		virtual void Terminate() {}

	private:

		EventLisenter<Engine::InitEvent> init_lisenter_;
		EventLisenter<Engine::StartUpEvent> start_up_lisenter_;
		EventLisenter<Engine::CloseDownEvent> close_down_lisenter_;
		EventLisenter<Engine::TerminateEvent> terminate_lisenter_;
		EventLisenter<Engine::EarlyUpdateEvent> early_update_lisenter_;
		EventLisenter<Engine::UpdateEvent> update_lisenter_;
		EventLisenter<Engine::LateUpdateEvent> late_update_lisenter_;
		EventLisenter<Engine::RenderEvent> render_lisenter_;
		EventLisenter<Engine::UIRenderEvent> ui_render_lisenter_;
	};
}
