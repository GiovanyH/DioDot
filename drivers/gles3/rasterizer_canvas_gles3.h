/*************************************************************************/
/*  rasterizer_canvas_gles3.h                                            */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef RASTERIZERCANVASGLES3_H
#define RASTERIZERCANVASGLES3_H

#include "rasterizer_storage_gles3.h"
#include "servers/visual/rasterizer.h"
#include "shaders/canvas_shadow.glsl.gen.h"

class RasterizerSceneGLES3;

class RasterizerCanvasGLES3 : public RasterizerCanvas {
public:
	struct CanvasItemUBO {

		float projection_matrix[16];
		float time;
		uint8_t padding[12];
	};

	RasterizerSceneGLES3 *scene_render;

	struct Data {

		GLuint canvas_quad_vertices;
		GLuint canvas_quad_array;

		GLuint polygon_buffer;
		GLuint polygon_buffer_quad_arrays[4];
		GLuint polygon_buffer_pointer_array;
		GLuint polygon_index_buffer;

		GLuint particle_quad_vertices;
		GLuint particle_quad_array;

		uint32_t polygon_buffer_size;

	} data;

	struct State {
		CanvasItemUBO canvas_item_ubo_data;
		GLuint canvas_item_ubo;
		bool canvas_texscreen_used;
		CanvasShaderGLES3 canvas_shader;
		CanvasShadowShaderGLES3 canvas_shadow_shader;

		bool using_texture_rect;
		bool using_ninepatch;

		RID current_tex;
		RID current_normal;
		RasterizerStorageGLES3::Texture *current_tex_ptr;

		Transform vp;

		Color canvas_item_modulate;
		Transform2D extra_matrix;
		Transform2D final_transform;

	} state;

	RasterizerStorageGLES3 *storage;

	struct LightInternal : public RID_Data {

		struct UBOData {

			float light_matrix[16];
			float local_matrix[16];
			float shadow_matrix[16];
			float color[4];
			float shadow_color[4];
			float light_pos[2];
			float shadowpixel_size;
			float shadow_gradient;
			float light_height;
			float light_outside_alpha;
			float shadow_distance_mult;
			uint8_t padding[4];
		} ubo_data;

		GLuint ubo;
	};

	RID_Owner<LightInternal> light_internal_owner;

	virtual RID light_internal_create();
	virtual void light_internal_update(RID p_rid, Light *p_light);
	virtual void light_internal_free(RID p_rid);

	virtual void canvas_begin();
	virtual void canvas_end();

	_FORCE_INLINE_ void _set_texture_rect_mode(bool p_enable, bool p_ninepatch = false);
	_FORCE_INLINE_ RasterizerStorageGLES3::Texture *_bind_canvas_texture(const RID &p_texture, const RID &p_normal_map, bool p_force = false);

	_FORCE_INLINE_ void _draw_gui_primitive(int p_points, const Vector2 *p_vertices, const Color *p_colors, const Vector2 *p_uvs);
	_FORCE_INLINE_ void _draw_polygon(const int *p_indices, int p_index_count, int p_vertex_count, const Vector2 *p_vertices, const Vector2 *p_uvs, const Color *p_colors, bool p_singlecolor);
	_FORCE_INLINE_ void _draw_generic(GLuint p_primitive, int p_vertex_count, const Vector2 *p_vertices, const Vector2 *p_uvs, const Color *p_colors, bool p_singlecolor);

	_FORCE_INLINE_ void _canvas_item_render_commands(Item *p_item, Item *current_clip, bool &reclip);
	_FORCE_INLINE_ void _copy_texscreen(const Rect2 &p_rect);

	virtual void canvas_render_items(Item *p_item_list, int p_z, const Color &p_modulate, Light *p_light);
	virtual void canvas_debug_viewport_shadows(Light *p_lights_with_shadow);

	virtual void canvas_light_shadow_buffer_update(RID p_buffer, const Transform2D &p_light_xform, int p_light_mask, float p_near, float p_far, LightOccluderInstance *p_occluders, CameraMatrix *p_xform_cache);

	virtual void reset_canvas();

	void draw_generic_textured_rect(const Rect2 &p_rect, const Rect2 &p_src);

	void initialize();
	void finalize();

	virtual void draw_window_margins(int *black_margin, RID *black_image);

	RasterizerCanvasGLES3();
};

#endif // RASTERIZERCANVASGLES3_H
