# Copyright 2020 Bitnine Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

MODULE_big = agensgraph

OBJS = src/backend/agensgraph.o \
       src/backend/catalog/ag_catalog.o \
       src/backend/catalog/ag_graph.o \
       src/backend/catalog/ag_label.o \
       src/backend/catalog/ag_namespace.o \
       src/backend/commands/graph_commands.o \
       src/backend/commands/label_commands.o \
       src/backend/executor/cypher_create.o \
       src/backend/nodes/ag_nodes.o \
       src/backend/nodes/outfuncs.o \
       src/backend/optimizer/cypher_createplan.o \
       src/backend/optimizer/cypher_pathnode.o \
       src/backend/optimizer/cypher_paths.o \
       src/backend/parser/ag_scanner.o \
       src/backend/parser/cypher_analyze.o \
       src/backend/parser/cypher_clause.o \
       src/backend/parser/cypher_expr.o \
       src/backend/parser/cypher_gram.o \
       src/backend/parser/cypher_item.o \
       src/backend/parser/cypher_keywords.o \
       src/backend/parser/cypher_parse_node.o \
       src/backend/parser/cypher_parser.o \
       src/backend/utils/adt/agtype.o \
       src/backend/utils/adt/agtype_ext.o \
       src/backend/utils/adt/agtype_ops.o \
       src/backend/utils/adt/agtype_parser.o \
       src/backend/utils/adt/agtype_util.o \
       src/backend/utils/adt/cypher_funcs.o \
       src/backend/utils/adt/graphid.o \
       src/backend/utils/ag_func.o

EXTENSION = agensgraph

DATA = agensgraph--0.0.0.sql

REGRESS = scan \
          agtype \
          commands \
          cypher \
          expr \
          cypher_create \
          cypher_with \

ag_regress_dir = $(srcdir)/regress
REGRESS_OPTS = --load-extension=agensgraph --inputdir=$(ag_regress_dir) --outputdir=$(ag_regress_dir) --temp-instance=$(ag_regress_dir)/instance --port=61958

ag_regress_out = instance/ log/ results/ regression.*
EXTRA_CLEAN = $(addprefix $(ag_regress_dir)/, $(ag_regress_out))

ag_include_dir = $(srcdir)/src/include
PG_CPPFLAGS = -I$(ag_include_dir)

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

src/backend/parser/cypher_gram.c: BISONFLAGS += --defines=$(ag_include_dir)/parser/$(basename $(notdir $@))_def.h

src/backend/parser/ag_scanner.c: FLEX_NO_BACKUP=yes
