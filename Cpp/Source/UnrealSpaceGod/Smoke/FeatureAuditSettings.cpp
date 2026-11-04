// FeatureAudit's settings probes (US-T50, US-T51), line for line the C# track's Runtime/Audit/SettingsAudit.cs.
#include "Smoke/FeatureAuditSettings.h"

#include "ActiveProfile.h"
#include "EngineSettings.h"
#include "GameSettingsHost.h"
#include "SessionAudio.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "UnityCompat/UnityRuntime.h"

#include "Persistence/Profiles.h"
#include "Rendering/RenderPipelineProfiles.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "Shared/Profiles/Profiles.h"

#include "AudioDevice.h"
#include "AudioThread.h"
#include "Engine/Engine.h"

namespace SpaceGod::Smoke::FeatureAudit
{
	using namespace SpaceGod::Core;
	using Rendering::RenderBackend;
	using Rendering::RenderPipelineSettings;
	using Rendering::UnrealRenderBackend;

	namespace
	{
		const char* B(bool v) { return v ? "True" : "False"; }

		cs::String V(float v) { return cs::Fmt(v, "0.##"); }

		bool StartsWith(const cs::String& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

		/// How to put a setting back as it is now: its value, or its default (forgotten in the profile) if it stands there.
		std::function<void()> Snapshot(const cs::Ref<GameSetting>& s)
		{
			if (s->IsDefault()) return [s] { s->Reset(); };
			if (auto t = cs::As<ToggleSetting>(s)) { bool v = t->Value; return [t, v] { t->Set(v); }; }
			if (auto c = cs::As<ChoiceSetting>(s)) { int v = c->Index; return [c, v] { c->Set(v); }; }
			if (auto r = cs::As<RangeSetting>(s)) { double v = r->Value; return [r, v] { r->Set(v); }; }
			return [] {};
		}

		/// The main audio device's transient primary volume (-1: no audio device), what ApplyAudio set: read on the audio
		/// thread (where the device keeps it) after every command queued before, ApplyAudio's included.
		float UnrealPrimaryVolume()
		{
			float volume = -1.0f;
			if (!GEngine) return volume;
			FAudioDeviceHandle Device = GEngine->GetMainAudioDevice();
			if (!Device) return volume;
			FAudioDevice* d = Device.GetAudioDevice();
			FAudioThread::RunCommandOnAudioThread([d, &volume] { volume = d->GetTransientPrimaryVolume(); });
			FAudioCommandFence fence;
			fence.BeginFence();
			fence.Wait();
			return volume;
		}

		/// RenderPipelineSelection.cs's and UrpPostProcessSettings.cs's settings, in declaration order.
		const cs::List<cs::String>& RenderKeys()
		{
			static cs::List<cs::String> keys{
				"render.pipeline", "render.hdrp.sky", "render.hdrp.volumetrics", "render.hdrp.exposure", "render.hdrp.exposureCompensation",
				"render.hdrp.sunBrightness", "render.hdrp.rayTracing",
				"render.urp.post", "render.urp.exposure", "render.urp.exposureCompensation", "render.urp.camera.iso", "render.urp.camera.shutter",
				"render.urp.camera.aperture", "render.urp.bloom", "render.urp.bloomThreshold", "render.urp.sunBrightness", "render.urp.tonemapping",
				"render.urp.contrast", "render.urp.saturation", "render.urp.temperature",
			};
			return keys;
		}

		cs::String Post()
		{
			UnrealRenderBackend* b = UnrealRenderBackend::Existing();
			return b ? b->Describe() : cs::String("no backend");
		}

		cs::String Photoreal()
		{
			PhotorealOptions p = RenderBackend::Active()->Photoreal();
			return cs::Cat("sky ", B(p.PhysicalSky), ", volumetrics ", B(p.Volumetrics), ", exposure ", static_cast<int>(p.Exposure),
				", compensation ", cs::Fmt(p.ExposureCompensation, "0.##"), ", glare ", cs::Fmt(p.SunBrightness, "0.#"), ", ray tracing ", B(p.RayTracing));
		}

		cs::String Stored(const cs::String& key)
		{
			cs::Nullable<cs::String> v = cs::New<ProfileSettingStore>(ActiveProfile::Current())->ReadSetting(key);
			return v ? *v : cs::String("(none)");
		}

		/// A setting moved off where it stands: a toggle flipped, a choice to the next, a range to its far end.
		void Bump(const cs::Ref<GameSetting>& s)
		{
			if (auto t = cs::As<ToggleSetting>(s)) t->Set(!t->Value);
			else if (auto c = cs::As<ChoiceSetting>(s)) c->Set((c->Index + 1) % c->Options.Count());
			else if (auto r = cs::As<RangeSetting>(s)) r->Set(r->Value < r->Max ? r->Max : r->Min);
		}

		/// One render setting moved and put back (SettingsAudit.RenderSetting): kept in the profile; a URP choice changes
		/// the post-process; HDRP's reach the photoreal options and leave the post-process alone; the pipeline is kept
		/// for the next start. ISO/shutter/aperture are tried under Exposure: Physical camera; Exposure goes from Fixed
		/// to Physical camera at ISO 400.
		void RenderSetting(const Report& result, const cs::Ref<SettingsRegistry>& registry, const cs::String& key)
		{
			cs::Ref<GameSetting> s = registry->Find(key);
			if (!s) { result(key, false, "not declared"); return; }
			cs::String initial = Post();
			std::vector<std::function<void()>> undo;
			auto prepare = [&](const char* k, int index)
			{
				if (auto c = cs::As<ChoiceSetting>(registry->Find(k)))
				{
					undo.push_back(Snapshot(c));
					c->Set(index);
				}
			};
			if (StartsWith(key, "render.urp.camera.")) prepare("render.urp.exposure", 3);
			if (key == "render.urp.exposure") prepare("render.urp.camera.iso", 3);
			undo.push_back(Snapshot(s));
			auto putBack = [&]
			{
				for (auto it = undo.rbegin(); it != undo.rend(); ++it) (*it)();
				cs::String back = Post();
				if (back != initial) result(cs::Cat(key, ".restore"), false, cs::Cat("put back: ", back, ", was ", initial));
			};
			try
			{
				cs::String before = Post(), photo0 = Photoreal(), text0 = s->ValueText();
				if (key == "render.urp.exposure") cs::As<ChoiceSetting>(s)->Set(3);
				else Bump(s);
				cs::String after = Post(), photo1 = Photoreal(), stored = Stored(key);
				bool kept = stored == s->Format(), same = after == before;
				cs::String moved = cs::Cat(text0, " -> ", s->ValueText(), " (kept '", stored, "')");
				if (key == RenderPipelineSettings::PipelineKey)
					result(key, kept && RenderPipelineSettings::RestartPending() && same,
						cs::Cat(moved, ": chosen ", RenderPipelineSettings::Chosen()->DisplayName, ", restart pending ", B(RenderPipelineSettings::RestartPending()),
							", post-process unchanged ", B(same)));
				else if (StartsWith(key, "render.hdrp."))
					result(key, kept && photo1 != photo0 && same, cs::Cat(moved, ": photoreal ", photo1, "; post-process unchanged ", B(same)));
				else
					result(key, kept && !same, cs::Cat(moved, ": ", before, " -> ", after));
			}
			catch (...)
			{
				putBack();
				throw;
			}
			putBack();
		}
	}

	void AudioProbes(const Report& result, const std::function<void(int)>& frames)
	{
		const auto& master = EngineSettings::MasterVolume();
		auto restore = Snapshot(master);
		bool m0 = Shared::Profiles::SessionMuted;
		auto putBack = [&]
		{
			Shared::Profiles::SessionMuted = m0;
			restore();
			UnityCompat::AudioListener::volume = SessionAudio::ListenerVolume(master->Value);
			frames(1);
		};
		try
		{
			master->Set(50);
			frames(1);
			float listener = UnityCompat::AudioListener::volume, unreal = UnrealPrimaryVolume();
			result("audio.volume", listener == SessionAudio::ListenerVolume(50) && unreal == listener,
				cs::Cat("master volume ", master->ValueText(), ": listener ", V(listener), ", Unreal primary volume ", V(unreal)));

			// What -muted / MINDATTIC_CLI_MUTED=1 turn on before the first scene: Profiles.SessionMuted, then SessionAudio.OnLoad.
			Shared::Profiles::SessionMuted = true;
			SessionAudio::OnLoad();
			frames(1);
			float l1 = UnityCompat::AudioListener::volume, u1 = UnrealPrimaryVolume();
			master->Set(80);
			frames(1);
			float l2 = UnityCompat::AudioListener::volume, u2 = UnrealPrimaryVolume();
			result("audio.mute", l1 == 0 && u1 == 0 && l2 == 0 && u2 == 0,
				cs::Cat("-muted: listener ", V(l1), ", Unreal ", V(u1), "; master volume ", master->ValueText(), " under the mute: listener ", V(l2), ", Unreal ", V(u2)));
		}
		catch (...)
		{
			putBack();
			throw;
		}
		putBack();
	}

	void RenderProbes(const Report& result, const std::function<void(int)>&)
	{
		const cs::Ref<SettingsRegistry>& registry = GameSettingsHost::Registry();
		cs::List<cs::String> inGroup;
		for (const auto& s : registry->InGroup(SettingGroups::RenderPipeline)) inGroup.Add(s->Key);
		int found = 0, at = -1;
		bool ordered = true;
		for (const auto& key : RenderKeys())
		{
			int i = inGroup.IndexOf(key);
			if (i < 0) continue;
			found++;
			if (i < at) ordered = false;
			at = i;
		}
		cs::String keys;
		for (int i = 0; i < inGroup.Count(); i++) keys += (i ? ", " : "") + inGroup[i];
		result("render.settings", found == RenderKeys().Count() && ordered,
			cs::Cat(found, "/", RenderKeys().Count(), " in '", SettingGroups::RenderPipeline, "', in order ", B(ordered), ": ", keys));

		for (const auto& key : RenderKeys()) RenderSetting(result, registry, key);
	}
}
