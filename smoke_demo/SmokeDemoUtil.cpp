#include "SmokeDemo.h"

namespace cubey {
	void SmokeDemo::PrepTextures() {
		DelTexture(s3d_velocity_);
		DelTexture(s3d_temperature_);
		DelTexture(s3d_density_);

		DelTexture(s3d_pressure_);

		DelTexture(t3d_divergence_);
		DelTexture(t3d_obstacle_);
		DelTexture(t3d_vorticity_);

		DelTexture(t3d_phi_n_1_hat_rgba_);
		DelTexture(t3d_phi_n_hat_rgba_);
		DelTexture(t3d_phi_n_1_hat_r_);
		DelTexture(t3d_phi_n_hat_r_);

		DelTexture(t3d_lighting_);

		DelTexture(t3d_density_blured_);
		DelTexture(t3d_lighting_blured_);
		DelTexture(t3d_temperature_blured_);
		DelTexture(t3d_obstacle_blured_);

		DelTexture(s3d_blur_r_);
		DelTexture(s3d_blur_rgba_);

		glm::ivec3 size = glm::ivec3(resolution_x(), resolution_y(), resolution_z());

		GenTexture(s3d_velocity_, GL_RGBA16F, size);
		GenTexture(s3d_temperature_, GL_R16F, size);
		GenTexture(s3d_density_, GL_RGBA16F, size);

		GenTexture(s3d_pressure_, GL_R16F, size);

		GenTexture(t3d_divergence_, GL_R16F, size);
		GenTexture(t3d_obstacle_, GL_R16F, size);
		GenTexture(t3d_vorticity_, GL_RGBA16F, size);

		GenTexture(t3d_phi_n_1_hat_rgba_, GL_RGBA16F, size);
		GenTexture(t3d_phi_n_hat_rgba_, GL_RGBA16F, size);
		GenTexture(t3d_phi_n_1_hat_r_, GL_R16F, size);
		GenTexture(t3d_phi_n_hat_r_, GL_R16F, size);

		GenTexture(t3d_lighting_, GL_R16F, size);

		GenTexture(t3d_density_blured_, GL_RGBA16F, size);
		GenTexture(t3d_lighting_blured_, GL_R16F, size);
		GenTexture(t3d_temperature_blured_, GL_R16F, size);
		GenTexture(t3d_obstacle_blured_, GL_R16F, size);

		GenTexture(s3d_blur_r_, GL_R16F, size);
		GenTexture(s3d_blur_rgba_, GL_RGBA16F, size);

		ClearTexture(s3d_velocity_.ping);
		ClearTexture(s3d_density_.ping);
		ClearTexture(s3d_temperature_.ping);
	}

	void SmokeDemo::GenTexture(Tex3D& tex, GLenum internal_format, const glm::ivec3& size) {
		tex.format = internal_format;
		tex.size = size;

		glActiveTexture(GL_TEXTURE0);
		glGenTextures(1, &tex.gl);
		glBindTexture(GL_TEXTURE_3D, tex.gl);
		glTexStorage3D(GL_TEXTURE_3D, 1, internal_format, size.x, size.y, size.z);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 0);
		glBindTexture(GL_TEXTURE_3D, 0);
	}

	void SmokeDemo::GenTexture(Slab& slab, GLenum internal_format, const glm::ivec3& size) {
		GenTexture(slab.ping, internal_format, size);
		GenTexture(slab.pong, internal_format, size);
	}

	void SmokeDemo::DelTexture(Tex3D& tex) {
		if (tex.gl) {
			glDeleteTextures(1, &tex.gl);
			tex.gl = 0;
		}
	}

	void SmokeDemo::DelTexture(Slab& slab) {
		DelTexture(slab.ping);
		DelTexture(slab.pong);
	}

	void SmokeDemo::ClearTexture(const Tex3D& tex) {
		ShaderProgram* p = tex.format == GL_RGBA16F ? sp_fill_rgba_ : sp_fill_r_;

		p->Activate();

		glBindImageTexture(0, tex.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, tex.format);
		glDispatchCompute(tex.size.x / 8, tex.size.y / 8, tex.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}

	std::vector<float> SmokeDemo::ComputeGaussianKernel(int size, float sigma) {
		std::vector<float> weights;

		float offset =  - (size - 1) / 2.0f;
		float sum = 0.0f;

		for (int i = 0; i < size; i++) {
			weights.push_back(glm::exp(- offset * offset / (sigma* sigma * 2.0f)));
			sum += weights[i];
			offset += 1.0f;
		}

		for (int i = 0; i < size; i++) {
			weights[i] /= sum;
		}

		return weights;
	}

	void SmokeDemo::GaussianBlur(const Tex3D& source, const Tex3D& target, float sigma) {
		ShaderProgram* p = (target.format == GL_RGBA16F ? sp_blur_rgba_ : sp_blur_r_);
		Slab slab = (target.format == GL_RGBA16F ? s3d_blur_rgba_ : s3d_blur_r_);

		p->Activate();
		p->SetUniform("u_weights[0]", ComputeGaussianKernel(5, sigma));

		p->SetUniform("u_pass_num", 0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, source.gl);

		glBindImageTexture(0, slab.ping.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, slab.ping.format);

		glDispatchCompute(slab.ping.size.x / 8, slab.ping.size.y / 8, slab.ping.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);

		p->SetUniform("u_pass_num", 1);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, slab.ping.gl);

		glBindImageTexture(0, slab.pong.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, slab.pong.format);

		glDispatchCompute(slab.ping.size.x / 8, slab.ping.size.y / 8, slab.ping.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		slab.Swap();

		p->SetUniform("u_pass_num", 2);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, slab.ping.gl);

		glBindImageTexture(0, target.gl, 0, GL_TRUE, 0, GL_WRITE_ONLY, target.format);

		glDispatchCompute(slab.ping.size.x / 8, slab.ping.size.y / 8, slab.ping.size.z / 8);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
	}
}