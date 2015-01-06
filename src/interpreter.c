/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#include "internal.h"

static double i_as_num(val_t v) {
  char *tmp;
  double dbl;
  if (!v7_is_double(v) && !v7_is_boolean(v)) {
    if (v7_is_string(v)) {
      struct v7_str *str = val_to_string(v);
      tmp = strndup(str->buf, str->len);
      dbl = strtod(tmp, NULL);
      free(tmp);
      return dbl;
    } else {
      return NAN;
    }
  } else {
    if(v7_is_boolean(v)) {
      return (double) val_to_boolean(v);
    }
    return val_to_double(v);
  }
}

static int i_is_true(val_t v) {
  /* TODO(mkm): real stuff */
  return (v7_is_double(v) && val_to_double(v) > 0.0) ||
      (v7_is_boolean(v) && val_to_boolean(v));
}

static double i_num_unary_op(enum ast_tag tag, double a) {
  switch (tag) {
    case AST_POSITIVE:
      return a;
    case AST_NEGATIVE:
      return -a;
    default:
      printf("NOT IMPLEMENTED");
      abort();
  }
}

static double i_num_bin_op(enum ast_tag tag, double a, double b) {
  switch (tag) {
    case AST_ADD:  /* simple fixed width nodes with no payload */
      return a + b;
    case AST_SUB:
      return a - b;
    case AST_REM:
      return (int) a % (int) b;
    case AST_MUL:
      return a * b;
    case AST_DIV:
      return a / b;
    default:
      printf("NOT IMPLEMENTED");
      abort();
  }
}

static int i_bool_bin_op(enum ast_tag tag, double a, double b) {
  switch (tag) {
    case AST_EQ:
      return a == b;
    case AST_NE:
      return a != b;
    case AST_LT:
      return a < b;
    case AST_LE:
      return a <= b;
    case AST_GT:
      return a > b;
    case AST_GE:
      return a >= b;
    default:
      printf("NOT IMPLEMENTED");
      abort();
  }
}

static val_t i_eval_expr(struct v7 *v7, struct ast *a, ast_off_t *pos,
                         val_t scope) {
  enum ast_tag tag = ast_fetch_tag(a, pos);
  const struct ast_node_def *def = &ast_node_defs[tag];
  ast_off_t end;
  val_t res, v1, v2;
  double dv;
  int i;

  /*
   * TODO(mkm): put this temporary somewhere in the evaluation context
   * or use alloca.
   */
  char buf[512];

  switch (tag) {
    case AST_NEGATIVE:
    case AST_POSITIVE:
      return v7_create_value(v7, V7_TYPE_NUMBER, i_num_unary_op(
          tag, i_as_num(i_eval_expr(v7, a, pos, scope))));
    case AST_ADD:
      v1 = i_eval_expr(v7, a, pos, scope);
      v2 = i_eval_expr(v7, a, pos, scope);
      if (!(v7_is_double(v1) || v7_is_boolean(v1)) ||
          !(v7_is_double(v2) || v7_is_boolean(v2))) {
        v7_stringify_value(v7, v1, buf, sizeof(buf));
        v7_stringify_value(v7, v2, buf + strlen(buf),
                           sizeof(buf) - strlen(buf));
        return v7_create_value(v7, V7_TYPE_STRING, buf,
                               (v7_strlen_t) strlen(buf));
      }
      return v7_create_value(v7, V7_TYPE_NUMBER, i_num_bin_op(tag, i_as_num(v1),
                                                              i_as_num(v2)));
    case AST_SUB:
    case AST_REM:
    case AST_MUL:
    case AST_DIV:
      v1 = i_eval_expr(v7, a, pos, scope);
      v2 = i_eval_expr(v7, a, pos, scope);
      return v7_create_value(v7, V7_TYPE_NUMBER, i_num_bin_op(tag, i_as_num(v1),
                                                              i_as_num(v2)));
    case AST_EQ:
    case AST_NE:
    case AST_LT:
    case AST_LE:
    case AST_GT:
    case AST_GE:
      v1 = i_eval_expr(v7, a, pos, scope);
      v2 = i_eval_expr(v7, a, pos, scope);
      return v7_create_value(v7, V7_TYPE_BOOLEAN,
                             i_bool_bin_op(tag, i_as_num(v1), i_as_num(v2)));
    case AST_ASSIGN:
      /* for now only simple assignment */
      assert((tag = ast_fetch_tag(a, pos)) == AST_IDENT);
      ast_get_inlined_data(a, *pos, buf, sizeof(buf));
      ast_move_to_children(a, pos);
      res = i_eval_expr(v7, a, pos, scope);
      v7_set_property_value(v7, scope, buf, -1, 0, res);
      return res;
    case AST_INDEX:
      v1 = i_eval_expr(v7, a, pos, scope);
      v2 = i_eval_expr(v7, a, pos, scope);
      v7_stringify_value(v7, v2, buf, sizeof(buf));
      return v7_property_value(v7_get_property(v1, buf, -1));
    case AST_MEMBER:
      ast_get_inlined_data(a, *pos, buf, sizeof(buf));
      ast_move_to_children(a, pos);
      v1 = i_eval_expr(v7, a, pos, scope);
      return v7_property_value(v7_get_property(v1, buf, -1));
    case AST_SEQ:
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      ast_move_to_children(a, pos);
      while (*pos < end) {
        res = i_eval_expr(v7, a, pos, scope);
      }
      return res;
    case AST_ARRAY:
      res = v7_create_value(v7, V7_TYPE_ARRAY_OBJECT);
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      ast_move_to_children(a, pos);
      for(i = 0; *pos < end; i++) {
        ast_off_t lookahead = *pos;
        enum ast_tag tag = ast_fetch_tag(a, &lookahead);
        v1 = i_eval_expr(v7, a, pos, scope);
        if (tag != AST_NOP) {
          snprintf(buf, sizeof(buf), "%d", i);
          v7_set_property_value(v7, res, buf, -1, 0, v1);
        }
      }
      return res;
    case AST_OBJECT:
      res = v7_create_value(v7, V7_TYPE_GENERIC_OBJECT);
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      ast_move_to_children(a, pos);
      while (*pos < end) {
        assert((tag = ast_fetch_tag(a, pos)) == AST_PROP);
        ast_get_inlined_data(a, *pos, buf, sizeof(buf));
        ast_move_to_children(a, pos);
        v1 = i_eval_expr(v7, a, pos, scope);
        v7_set_property_value(v7, res, buf, -1, 0, v1);
      }
      return res;
    case AST_TRUE:
      return v7_create_value(v7, V7_TYPE_BOOLEAN, 1);
    case AST_FALSE:
      return v7_create_value(v7, V7_TYPE_BOOLEAN, 0);
    case AST_NULL:
      return v7_create_value(v7, V7_TYPE_NULL);
    case AST_NOP:
    case AST_UNDEFINED:
      return v7_create_value(v7, V7_TYPE_UNDEFINED);
    case AST_NUM:
      ast_get_num(a, *pos, &dv);
      ast_move_to_children(a, pos);
      return v7_create_value(v7, V7_TYPE_NUMBER, dv);
    case AST_STRING:
      ast_get_inlined_data(a, *pos, buf, sizeof(buf));
      ast_move_to_children(a, pos);
      res = v7_create_value(v7, V7_TYPE_STRING, buf,
                            (v7_strlen_t) strlen(buf), 1);
      return res;
    case AST_IDENT:
      ast_get_inlined_data(a, *pos, buf, sizeof(buf));
      ast_move_to_children(a, pos);
      res = v7_property_value(v7_get_property(scope, buf, -1));
      if (res == V7_UNDEFINED) {
        fprintf(stderr, "ReferenceError: %s is not defined\n", buf);
        abort();
      }
      return res;
    case AST_FUNC:
      {
        val_t func = v7_create_value(v7, V7_TYPE_FUNCTION_OBJECT);
        struct v7_function *funcp = val_to_function(func);
        funcp->ast = a;
        funcp->ast_off = *pos - 1;
        *pos = ast_get_skip(a, *pos, AST_END_SKIP);
        return func;
      }
    default:
      printf("NOT IMPLEMENTED: %s\n", def->name);
      abort();
  }
}

static val_t i_eval_stmt(struct v7 *, struct ast *, ast_off_t *, val_t);

static val_t i_eval_stmts(struct v7 *v7, struct ast *a, ast_off_t *pos,
                          ast_off_t end, val_t scope) {
  val_t res;
  while (*pos < end) {
    res = i_eval_stmt(v7, a, pos, scope);
  }
  return res;
}

static val_t i_eval_stmt(struct v7 *v7, struct ast *a, ast_off_t *pos,
                         val_t scope) {
  enum ast_tag tag = ast_fetch_tag(a, pos);
  val_t res;
  ast_off_t end, cond, iter_end, loop, iter, finally, catch;

  switch (tag) {
    case AST_SCRIPT: /* TODO(mkm): push up */
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      ast_move_to_children(a, pos);
      while (*pos < end) {
        res = i_eval_stmt(v7, a, pos, scope);
      }
      return res;
    case AST_IF:
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      ast_move_to_children(a, pos);
      if (i_is_true(i_eval_expr(v7, a, pos, scope))) {
        i_eval_stmts(v7, a, pos, end, scope);
      }
      *pos = end;
      break;
    case AST_WHILE:
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      ast_move_to_children(a, pos);
      cond = *pos;
      for (;;) {
        if (i_is_true(i_eval_expr(v7, a, pos, scope))) {
          i_eval_stmts(v7, a, pos, end, scope);
        } else {
          *pos = end;
          break;
        }
        *pos = cond;
      }
      break;
    case AST_DOWHILE:
      end = ast_get_skip(a, *pos, AST_DO_WHILE_COND_SKIP);
      ast_move_to_children(a, pos);
      loop = *pos;
      for (;;) {
        i_eval_stmts(v7, a, pos, end, scope);
        if (!i_is_true(i_eval_expr(v7, a, pos, scope))) {
          break;
        }
        *pos = loop;
      }
      break;
    case AST_FOR:
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      iter_end = ast_get_skip(a, *pos, AST_FOR_BODY_SKIP);
      ast_move_to_children(a, pos);
      /* initializer */
      i_eval_expr(v7, a, pos, scope);
      for (;;) {
        loop = *pos;
        if (!i_is_true(i_eval_expr(v7, a, &loop, scope))) {
          *pos = end;
          return v7_create_value(v7, V7_TYPE_UNDEFINED);
        }
        iter = loop;
        loop = iter_end;
        i_eval_stmts(v7, a, &loop, end, scope);
        i_eval_expr(v7, a, &iter, scope);
      }
    case AST_TRY:
      /* Dummy no catch impl */
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      catch = ast_get_skip(a, *pos, AST_TRY_CATCH_SKIP);
      finally = ast_get_skip(a, *pos, AST_TRY_FINALLY_SKIP);
      ast_move_to_children(a, pos);
      i_eval_stmts(v7, a, pos, catch, scope);
      if (finally) {
        i_eval_stmts(v7, a, &finally, end, scope);
      }
      *pos = end;
      break;
    case AST_WITH:
      end = ast_get_skip(a, *pos, AST_END_SKIP);
      ast_move_to_children(a, pos);
      /*
       * TODO(mkm) make an actual scope chain. Possibly by mutating
       * the with expression and adding the 'outer environment
       * reference' hidden property.
       */
      i_eval_stmts(v7, a, pos, end, i_eval_expr(v7, a, pos, scope));
      break;
    default:
      (*pos)--;
      return i_eval_expr(v7, a, pos, scope);
  }
  return v7_create_value(v7, V7_TYPE_UNDEFINED);
}

V7_PRIVATE val_t v7_exec_2(struct v7 *v7, const char* src) {
  /* TODO(mkm): use GC pool */
  struct ast *a = (struct ast *) malloc(sizeof(struct ast));
  val_t res;
  ast_off_t pos = 0;
  char debug[1024];

  ast_init(a, 0);
  if (aparse(a, src, 1) != V7_OK) {
    printf("Error parsing\n");
    return V7_UNDEFINED;
  }
  ast_optimize(a);

#if 0
  ast_dump(stdout, a, 0);
#endif

  res = i_eval_stmt(v7, a, &pos, v7->global_object);
  v7_to_json(v7, res, debug, sizeof(debug));
#if 0
  fprintf(stderr, "Eval res: %s .\n", debug);
#endif
  return res;
}
