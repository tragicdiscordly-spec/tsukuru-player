/* Runs a JavaScript file with QuickJS-NG twice, once with the system malloc and once with the pool allocator
 * from the Outsider port (js_pool_alloc.h), and prints its output. Used to measure how fast the engine is on
 * the PS5. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"
#include "js_pool_alloc.h"

static JSValue js_print(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
	for (int i = 0; i < argc; i++) {
		const char* s = JS_ToCString(ctx, argv[i]);
		if (s) { fputs(s, stdout); JS_FreeCString(ctx, s); }
		if (i + 1 < argc) fputc(' ', stdout);
	}
	fputc('\n', stdout);
	fflush(stdout);
	return JS_UNDEFINED;
}

static int run_once(const char* label, const char* path, const char* src, long size, int use_pool) {
	printf("=== %s ===\n", label);
	fflush(stdout);
	JSRuntime* rt = use_pool ? JS_NewRuntime2(&js_pool_malloc_functions, NULL) : JS_NewRuntime();
	JSContext* ctx = JS_NewContext(rt);
	JSValue global = JS_GetGlobalObject(ctx);
	JS_SetPropertyStr(ctx, global, "print", JS_NewCFunction(ctx, js_print, "print", 1));
	JS_FreeValue(ctx, global);
	JSValue r = JS_Eval(ctx, src, size, path, JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r)) {
		JSValue ex = JS_GetException(ctx);
		const char* s = JS_ToCString(ctx, ex);
		fprintf(stderr, "JS error: %s\n", s ? s : "?");
		return 1;
	}
	JS_FreeValue(ctx, r);
	JS_FreeContext(ctx);
	JS_FreeRuntime(rt);
	return 0;
}

int main(int argc, char** argv) {
	const char* path = argc > 1 ? argv[1] : "bench.js";
	FILE* f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
	fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
	char* src = malloc(size + 1);
	if (fread(src, 1, size, f) != (size_t)size) { fprintf(stderr, "read failed\n"); return 1; }
	src[size] = 0; fclose(f);

	if (run_once("system malloc", path, src, size, 0)) return 1;
	if (run_once("pool allocator", path, src, size, 1)) return 1;
	return 0;
}
