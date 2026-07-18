#include "frame_pipeline.hpp"

#include <shark/core/error.hpp>

#include <string>
#include <utility>

namespace shark::renderer::detail {
namespace {

[[nodiscard]] core::Error pipeline_error(std::string message)
{
    return core::Error{
        core::ErrorCategory::graphics,
        core::ErrorCode::invalid_argument,
        std::move(message),
    };
}

template<typename T>
[[nodiscard]] core::Result<render_graph::CompiledGraph>
pipeline_failure(core::Result<T> result)
{
    return core::Result<render_graph::CompiledGraph>::failure(
        std::move(result).error());
}

} // namespace

core::Result<render_graph::CompiledGraph> compose_frame_pipeline(
    FramePipelineCallbacks callbacks)
{
    if (!callbacks.terrain) {
        return core::Result<render_graph::CompiledGraph>::failure(
            pipeline_error(
                "Renderer frame pipeline requires a Terrain callback"));
    }
    if (!callbacks.textured_cube) {
        return core::Result<render_graph::CompiledGraph>::failure(
            pipeline_error(
                "Renderer frame pipeline requires a TexturedCube callback"));
    }
    if (!callbacks.skybox) {
        return core::Result<render_graph::CompiledGraph>::failure(
            pipeline_error(
                "Renderer frame pipeline requires a Skybox callback"));
    }
    if (!callbacks.tone_map) {
        return core::Result<render_graph::CompiledGraph>::failure(
            pipeline_error(
                "Renderer frame pipeline requires a ToneMap callback"));
    }

    render_graph::GraphBuilder builder;
    auto back_buffer_result = builder.import_resource(
        "BackBuffer",
        frame_back_buffer_external_id,
        render_graph::ResourceState::present,
        render_graph::ResourceState::present);
    if (!back_buffer_result) {
        return pipeline_failure(std::move(back_buffer_result));
    }
    const auto back_buffer = back_buffer_result.value();

    auto scene_color_result = builder.import_resource(
        "SceneColor",
        frame_scene_color_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!scene_color_result) {
        return pipeline_failure(std::move(scene_color_result));
    }
    const auto scene_color = scene_color_result.value();

    auto depth_buffer_result = builder.import_resource(
        "DepthBuffer",
        frame_depth_buffer_external_id,
        render_graph::ResourceState::depth_write,
        render_graph::ResourceState::depth_write);
    if (!depth_buffer_result) {
        return pipeline_failure(std::move(depth_buffer_result));
    }
    const auto depth_buffer = depth_buffer_result.value();

    auto checker_texture_result = builder.import_resource(
        "CheckerTexture",
        frame_checker_texture_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!checker_texture_result) {
        return pipeline_failure(std::move(checker_texture_result));
    }
    const auto checker_texture = checker_texture_result.value();

    auto cube_vertex_buffer_result = builder.import_resource(
        "CubeVertexBuffer",
        frame_cube_vertex_buffer_external_id,
        render_graph::ResourceState::vertex_buffer,
        render_graph::ResourceState::vertex_buffer);
    if (!cube_vertex_buffer_result) {
        return pipeline_failure(std::move(cube_vertex_buffer_result));
    }
    const auto cube_vertex_buffer =
        cube_vertex_buffer_result.value();

    auto cube_index_buffer_result = builder.import_resource(
        "CubeIndexBuffer",
        frame_cube_index_buffer_external_id,
        render_graph::ResourceState::index_buffer,
        render_graph::ResourceState::index_buffer);
    if (!cube_index_buffer_result) {
        return pipeline_failure(std::move(cube_index_buffer_result));
    }
    const auto cube_index_buffer =
        cube_index_buffer_result.value();

    auto terrain_vertex_buffer_result = builder.import_resource(
        "TerrainVertexBuffer",
        frame_terrain_vertex_buffer_external_id,
        render_graph::ResourceState::vertex_buffer,
        render_graph::ResourceState::vertex_buffer);
    if (!terrain_vertex_buffer_result) {
        return pipeline_failure(std::move(terrain_vertex_buffer_result));
    }
    const auto terrain_vertex_buffer =
        terrain_vertex_buffer_result.value();

    auto terrain_index_buffer_result = builder.import_resource(
        "TerrainIndexBuffer",
        frame_terrain_index_buffer_external_id,
        render_graph::ResourceState::index_buffer,
        render_graph::ResourceState::index_buffer);
    if (!terrain_index_buffer_result) {
        return pipeline_failure(std::move(terrain_index_buffer_result));
    }
    const auto terrain_index_buffer =
        terrain_index_buffer_result.value();

    auto terrain_albedo_layers_result = builder.import_resource(
        "TerrainAlbedoLayers",
        frame_terrain_albedo_layers_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_albedo_layers_result) {
        return pipeline_failure(std::move(terrain_albedo_layers_result));
    }
    const auto terrain_albedo_layers =
        terrain_albedo_layers_result.value();

    auto terrain_normal_layers_result = builder.import_resource(
        "TerrainNormalLayers",
        frame_terrain_normal_layers_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_normal_layers_result) {
        return pipeline_failure(std::move(terrain_normal_layers_result));
    }
    const auto terrain_normal_layers =
        terrain_normal_layers_result.value();

    auto terrain_roughness_layers_result = builder.import_resource(
        "TerrainRoughnessLayers",
        frame_terrain_roughness_layers_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_roughness_layers_result) {
        return pipeline_failure(std::move(terrain_roughness_layers_result));
    }
    const auto terrain_roughness_layers =
        terrain_roughness_layers_result.value();

    auto environment_radiance_result = builder.import_resource(
        "EnvironmentRadiance",
        frame_environment_radiance_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!environment_radiance_result) {
        return pipeline_failure(std::move(environment_radiance_result));
    }
    const auto environment_radiance =
        environment_radiance_result.value();

    auto environment_irradiance_result = builder.import_resource(
        "EnvironmentIrradiance",
        frame_environment_irradiance_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!environment_irradiance_result) {
        return pipeline_failure(std::move(environment_irradiance_result));
    }
    const auto environment_irradiance =
        environment_irradiance_result.value();

    auto environment_prefiltered_specular_result =
        builder.import_resource(
            "EnvironmentPrefilteredSpecular",
            frame_environment_prefiltered_specular_external_id,
            render_graph::ResourceState::pixel_shader_read,
            render_graph::ResourceState::pixel_shader_read);
    if (!environment_prefiltered_specular_result) {
        return pipeline_failure(
            std::move(environment_prefiltered_specular_result));
    }
    const auto environment_prefiltered_specular =
        environment_prefiltered_specular_result.value();

    auto environment_brdf_lut_result = builder.import_resource(
        "EnvironmentBrdfLut",
        frame_environment_brdf_lut_external_id,
        render_graph::ResourceState::pixel_shader_read,
        render_graph::ResourceState::pixel_shader_read);
    if (!environment_brdf_lut_result) {
        return pipeline_failure(std::move(environment_brdf_lut_result));
    }
    const auto environment_brdf_lut =
        environment_brdf_lut_result.value();

    const TerrainPassResources terrain_resources{
        scene_color,
        depth_buffer,
        terrain_vertex_buffer,
        terrain_index_buffer,
        terrain_albedo_layers,
        terrain_normal_layers,
        terrain_roughness_layers,
        environment_irradiance,
        environment_prefiltered_specular,
        environment_brdf_lut,
    };
    auto terrain_pass_result = builder.add_pass(
        "Terrain",
        [callback = std::move(callbacks.terrain),
         terrain_resources](
            const render_graph::PassContext& context) {
            return callback(context, terrain_resources);
        });
    if (!terrain_pass_result) {
        return pipeline_failure(std::move(terrain_pass_result));
    }
    const auto terrain_pass = terrain_pass_result.value();

    auto terrain_color_result = builder.write(
        terrain_pass,
        scene_color,
        render_graph::ResourceState::render_target);
    if (!terrain_color_result) {
        return pipeline_failure(std::move(terrain_color_result));
    }
    auto terrain_depth_result = builder.write(
        terrain_pass,
        depth_buffer,
        render_graph::ResourceState::depth_write);
    if (!terrain_depth_result) {
        return pipeline_failure(std::move(terrain_depth_result));
    }
    auto terrain_vertex_result = builder.read(
        terrain_pass,
        terrain_vertex_buffer,
        render_graph::ResourceState::vertex_buffer);
    if (!terrain_vertex_result) {
        return pipeline_failure(std::move(terrain_vertex_result));
    }
    auto terrain_index_result = builder.read(
        terrain_pass,
        terrain_index_buffer,
        render_graph::ResourceState::index_buffer);
    if (!terrain_index_result) {
        return pipeline_failure(std::move(terrain_index_result));
    }
    auto terrain_albedo_result = builder.read(
        terrain_pass,
        terrain_albedo_layers,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_albedo_result) {
        return pipeline_failure(std::move(terrain_albedo_result));
    }
    auto terrain_normal_result = builder.read(
        terrain_pass,
        terrain_normal_layers,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_normal_result) {
        return pipeline_failure(std::move(terrain_normal_result));
    }
    auto terrain_roughness_result = builder.read(
        terrain_pass,
        terrain_roughness_layers,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_roughness_result) {
        return pipeline_failure(std::move(terrain_roughness_result));
    }
    auto terrain_irradiance_result = builder.read(
        terrain_pass,
        environment_irradiance,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_irradiance_result) {
        return pipeline_failure(std::move(terrain_irradiance_result));
    }
    auto terrain_prefiltered_specular_result = builder.read(
        terrain_pass,
        environment_prefiltered_specular,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_prefiltered_specular_result) {
        return pipeline_failure(
            std::move(terrain_prefiltered_specular_result));
    }
    auto terrain_brdf_lut_result = builder.read(
        terrain_pass,
        environment_brdf_lut,
        render_graph::ResourceState::pixel_shader_read);
    if (!terrain_brdf_lut_result) {
        return pipeline_failure(std::move(terrain_brdf_lut_result));
    }

    const TexturedCubePassResources textured_cube_resources{
        scene_color,
        depth_buffer,
        checker_texture,
        cube_vertex_buffer,
        cube_index_buffer,
    };
    auto textured_cube_pass_result = builder.add_pass(
        "TexturedCube",
        [callback = std::move(callbacks.textured_cube),
         textured_cube_resources](
            const render_graph::PassContext& context) {
            return callback(context, textured_cube_resources);
        });
    if (!textured_cube_pass_result) {
        return pipeline_failure(std::move(textured_cube_pass_result));
    }
    const auto textured_cube_pass =
        textured_cube_pass_result.value();

    auto cube_color_result = builder.write(
        textured_cube_pass,
        scene_color,
        render_graph::ResourceState::render_target);
    if (!cube_color_result) {
        return pipeline_failure(std::move(cube_color_result));
    }
    auto cube_depth_result = builder.write(
        textured_cube_pass,
        depth_buffer,
        render_graph::ResourceState::depth_write);
    if (!cube_depth_result) {
        return pipeline_failure(std::move(cube_depth_result));
    }
    auto checker_result = builder.read(
        textured_cube_pass,
        checker_texture,
        render_graph::ResourceState::pixel_shader_read);
    if (!checker_result) {
        return pipeline_failure(std::move(checker_result));
    }
    auto cube_vertex_result = builder.read(
        textured_cube_pass,
        cube_vertex_buffer,
        render_graph::ResourceState::vertex_buffer);
    if (!cube_vertex_result) {
        return pipeline_failure(std::move(cube_vertex_result));
    }
    auto cube_index_result = builder.read(
        textured_cube_pass,
        cube_index_buffer,
        render_graph::ResourceState::index_buffer);
    if (!cube_index_result) {
        return pipeline_failure(std::move(cube_index_result));
    }

    const SkyboxPassResources skybox_resources{
        scene_color,
        depth_buffer,
        cube_vertex_buffer,
        cube_index_buffer,
        environment_radiance,
    };
    auto skybox_pass_result = builder.add_pass(
        "Skybox",
        [callback = std::move(callbacks.skybox),
         skybox_resources](
            const render_graph::PassContext& context) {
            return callback(context, skybox_resources);
        });
    if (!skybox_pass_result) {
        return pipeline_failure(std::move(skybox_pass_result));
    }
    const auto skybox_pass = skybox_pass_result.value();

    auto skybox_color_result = builder.write(
        skybox_pass,
        scene_color,
        render_graph::ResourceState::render_target);
    if (!skybox_color_result) {
        return pipeline_failure(std::move(skybox_color_result));
    }
    auto skybox_depth_result = builder.read(
        skybox_pass,
        depth_buffer,
        render_graph::ResourceState::depth_read);
    if (!skybox_depth_result) {
        return pipeline_failure(std::move(skybox_depth_result));
    }
    auto skybox_vertex_result = builder.read(
        skybox_pass,
        cube_vertex_buffer,
        render_graph::ResourceState::vertex_buffer);
    if (!skybox_vertex_result) {
        return pipeline_failure(std::move(skybox_vertex_result));
    }
    auto skybox_index_result = builder.read(
        skybox_pass,
        cube_index_buffer,
        render_graph::ResourceState::index_buffer);
    if (!skybox_index_result) {
        return pipeline_failure(std::move(skybox_index_result));
    }
    auto skybox_radiance_result = builder.read(
        skybox_pass,
        environment_radiance,
        render_graph::ResourceState::pixel_shader_read);
    if (!skybox_radiance_result) {
        return pipeline_failure(std::move(skybox_radiance_result));
    }

    const ToneMapPassResources tone_map_resources{
        back_buffer,
        scene_color,
    };
    auto tone_map_pass_result = builder.add_pass(
        "ToneMap",
        [callback = std::move(callbacks.tone_map),
         tone_map_resources](
            const render_graph::PassContext& context) {
            return callback(context, tone_map_resources);
        });
    if (!tone_map_pass_result) {
        return pipeline_failure(std::move(tone_map_pass_result));
    }
    const auto tone_map_pass = tone_map_pass_result.value();

    auto tone_map_color_result = builder.write(
        tone_map_pass,
        back_buffer,
        render_graph::ResourceState::render_target);
    if (!tone_map_color_result) {
        return pipeline_failure(std::move(tone_map_color_result));
    }
    auto tone_map_scene_result = builder.read(
        tone_map_pass,
        scene_color,
        render_graph::ResourceState::pixel_shader_read);
    if (!tone_map_scene_result) {
        return pipeline_failure(std::move(tone_map_scene_result));
    }

    return std::move(builder).compile();
}

} // namespace shark::renderer::detail
