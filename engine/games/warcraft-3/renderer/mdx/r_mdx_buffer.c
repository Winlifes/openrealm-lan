#include "r_mdx.h"

typedef struct { uint32_t count; int first; } mdxPaletteError_t;

void MDLX_ReleaseModelGeoset(mdxGeoset_t *geoset);

/* The on-disk group index can exceed 255; only the compact draw palette is
 * byte-sized. A modern weighted vertex references the first node of each group. */
static uint32_t mdx_vertex_nodes(mdxGeoset_t const *geo, uint32_t vertex,
                                 uint32_t const *offsets, int nodes[MAX_SKIN_BONES]) {
    uint32_t count=0;
    if (geo->skin && ARRAY_COUNT(geo->skin)==(uint32_t)geo->num_vertices*8) {
        FOR_LOOP(k,4) {
            uint32_t group=geo->skin[vertex*8+k];
            if (geo->skin[vertex*8+k+4] && group<(uint32_t)geo->num_matrixGroupSizes &&
                offsets[group]<(uint32_t)geo->num_matrices) nodes[count++]=geo->matrices[offsets[group]];
        }
    } else {
        uint32_t group=geo->vertexGroups ? (uint8_t)geo->vertexGroups[vertex] : 0;
        if (group<(uint32_t)geo->num_matrixGroupSizes) {
            uint32_t n=MIN(geo->matrixGroupSizes[group],MAX_SKIN_BONES);
            FOR_LOOP(k,n) if (offsets[group]+k<(uint32_t)geo->num_matrices) nodes[count++]=geo->matrices[offsets[group]+k];
        }
    }
    if (!count) nodes[count++]=0;
    return count;
}

static mdxGeoset_t *mdx_copy_triangle_range(mdxGeoset_t const *src, uint32_t first, uint32_t limit) {
    int *remap=ri.MemAlloc(src->num_vertices*sizeof(*remap));
    memset(remap,0xff,src->num_vertices*sizeof(*remap));
    uint32_t vertices=0;
    for (uint32_t i=first;i<limit;i++) {
        uint16_t v=(uint16_t)src->triangles[i];
        if (remap[v]<0) remap[v]=vertices++;
    }
    mdxGeoset_t *geo=ri.MemAlloc(sizeof(*geo));
    geo->materialID=src->materialID;geo->group=src->group;geo->selectable=src->selectable;
    geo->default_bounds=src->default_bounds;geo->geosetAnim=src->geosetAnim;geo->lod=src->lod;
    memcpy(geo->lodName,src->lodName,sizeof(geo->lodName));
    geo->num_vertices=geo->num_normals=geo->num_texcoord=vertices;
    geo->num_texcoordChannels=src->num_texcoordChannels;
    geo->vertices=ri.MemAlloc(vertices*sizeof(vec3_t));geo->normals=ri.MemAlloc(vertices*sizeof(vec3_t));
    geo->texcoord=ri.MemAlloc(vertices*MAX(1,src->num_texcoordChannels)*sizeof(vec2_t));
    geo->num_triangles=limit-first;geo->triangles=ri.MemAlloc(geo->num_triangles*sizeof(short));
    geo->num_vertexGroups=vertices;geo->vertexGroups=ri.MemAlloc(vertices);
    if (src->skin) {ARRAY_COUNT(geo->skin)=vertices*8;geo->skin=ri.MemAlloc(ARRAY_COUNT(geo->skin)*sizeof(uint16_t));}
    if (src->tangents) {ARRAY_COUNT(geo->tangents)=vertices;geo->tangents=ri.MemAlloc(vertices*sizeof(vec4_t));}
    FOR_LOOP(i,src->num_vertices) if (remap[i]>=0) {
        uint32_t v=remap[i];
        geo->vertices[v]=src->vertices[i];geo->normals[v]=src->normals[i];
        /* Each UV set has its own vertex stride; preserve every authored set
         * when the bone palette partitions a geoset into smaller batches. */
        FOR_LOOP(uv,MAX(1,src->num_texcoordChannels))
            geo->texcoord[uv*vertices+v]=src->texcoord[uv*src->num_vertices+i];
        if(src->vertexGroups)geo->vertexGroups[v]=src->vertexGroups[i];
        if(src->skin)memcpy(geo->skin+v*8,src->skin+i*8,8*sizeof(uint16_t));
        if(src->tangents)geo->tangents[v]=src->tangents[i];
    }
    for(uint32_t i=first;i<limit;i++)geo->triangles[i-first]=(short)remap[(uint16_t)src->triangles[i]];
    geo->num_matrices=src->num_matrices;geo->matrices=ri.MemAlloc(src->num_matrices*sizeof(int));
    memcpy(geo->matrices,src->matrices,src->num_matrices*sizeof(int));
    geo->num_matrixGroupSizes=src->num_matrixGroupSizes;geo->matrixGroupSizes=ri.MemAlloc(src->num_matrixGroupSizes*sizeof(int));
    memcpy(geo->matrixGroupSizes,src->matrixGroupSizes,src->num_matrixGroupSizes*sizeof(int));
    geo->num_bounds=src->num_bounds;geo->bounds=ri.MemAlloc(src->num_bounds*sizeof(mdxBounds_t));
    memcpy(geo->bounds,src->bounds,src->num_bounds*sizeof(mdxBounds_t));
    ri.MemFree(remap);return geo;
}

/* Keep the shared 128-matrix shader contract. Partition whole triangles,
 * duplicating shared vertices at batch boundaries, instead of dropping bones. */
static void MDX_PartitionGeometry(mdxModel_t *model) {
    for(mdxGeoset_t **link=&model->geosets;*link;) {
        mdxGeoset_t *src=*link;
        uint32_t groupCount=MAX(0,src->num_matrixGroupSizes);
        uint32_t *offsets=ri.MemAlloc((groupCount+1)*sizeof(*offsets));offsets[0]=0;
        FOR_LOOP(i,groupCount)offsets[i+1]=offsets[i]+src->matrixGroupSizes[i];
        bool seen[MDX_MAX_NODES]={0};uint32_t total=0;
        FOR_LOOP(v,src->num_vertices) {
            int nodes[MAX_SKIN_BONES];uint32_t n=mdx_vertex_nodes(src,v,offsets,nodes);
            FOR_LOOP(k,n)if(nodes[k]>=0&&nodes[k]<MDX_MAX_NODES&&!seen[nodes[k]]){seen[nodes[k]]=true;total++;}
        }
        if(total<=MDX_MATRIX_PALETTE||src->num_triangles<3){ri.MemFree(offsets);link=&src->next;continue;}
        memset(seen,0,sizeof(seen));total=0;
        uint32_t first=0;mdxGeoset_t *head=NULL,**tail=&head;
        for(uint32_t triangle=0;triangle<(uint32_t)src->num_triangles;triangle+=3) {
            int added[3*MAX_SKIN_BONES];uint32_t additions=0;
            FOR_LOOP(corner,3) {
                uint16_t v=(uint16_t)src->triangles[triangle+corner];int nodes[MAX_SKIN_BONES];
                uint32_t n=mdx_vertex_nodes(src,v,offsets,nodes);
                FOR_LOOP(k,n) {
                    int id=nodes[k];if(id<0||id>=MDX_MAX_NODES||seen[id])continue;
                    bool duplicate=false;FOR_LOOP(a,additions)if(added[a]==id)duplicate=true;
                    if(!duplicate)added[additions++]=id;
                }
            }
            if(total+additions>MDX_MATRIX_PALETTE) {
                *tail=mdx_copy_triangle_range(src,first,triangle);tail=&(*tail)->next;
                memset(seen,0,sizeof(seen));total=0;first=triangle;triangle-=3;continue;
            }
            FOR_LOOP(a,additions)seen[added[a]]=true;total+=additions;
        }
        *tail=mdx_copy_triangle_range(src,first,src->num_triangles);tail=&(*tail)->next;
        *tail=src->next;*link=head;src->next=NULL;MDLX_ReleaseModelGeoset(src);
        ri.MemFree(offsets);link=tail;
    }
}

static uint8_t R_AddGeosetMatrixPaletteEntry(mdxGeoset_t *geoset, int matrix_id, mdxPaletteError_t *overflow) {
    if (matrix_id < 0) {
        matrix_id = 0;
    }
    FOR_LOOP(i, geoset->num_matrixPalette) {
        if (geoset->matrixPalette[i] == matrix_id) {
            return (uint8_t)i;
        }
    }
    if (geoset->num_matrixPalette >= MDX_MATRIX_PALETTE) {
        if (!overflow->count)
            overflow->first = matrix_id;
        overflow->count++;
        return 0;
    }
    geoset->matrixPalette[geoset->num_matrixPalette] = matrix_id;
    return (uint8_t)geoset->num_matrixPalette++;
}

/* Keep geoset-local palette indices while packing every stream into the shared vertex format. */
static void mdx_pack_vertices(mdxGeoset_t *geoset, vertex_t *vertices) {
    uint32_t groupCount = geoset->num_matrixGroupSizes > 0 ? geoset->num_matrixGroupSizes : 0;
    uint32_t *groupOffsets = ri.MemAlloc((groupCount + 1) * sizeof(*groupOffsets));
    groupOffsets[0] = 0;
    FOR_LOOP(i, groupCount) groupOffsets[i + 1] = groupOffsets[i] + geoset->matrixGroupSizes[i];
    mdxPaletteError_t overflow = {0};
    geoset->matrixPalette = ri.MemAlloc(sizeof(*geoset->matrixPalette) * MDX_MATRIX_PALETTE);
    geoset->num_matrixPalette = 0;

    FOR_LOOP(vertex, geoset->num_vertices) {
        vertices[vertex].position = geoset->vertices[vertex];
        vertices[vertex].normal = geoset->normals[vertex];
        vertices[vertex].texcoord = geoset->texcoord[vertex];
        vertices[vertex].color = COLOR32_WHITE;
        uint8_t rawSkin[MAX_SKIN_BONES] = {0}, rawWeight[MAX_SKIN_BONES] = {0};
        if (geoset->skin && ARRAY_COUNT(geoset->skin) == (uint32_t)geoset->num_vertices * 8) {
            FOR_LOOP(k, 4) {
                uint16_t group = geoset->skin[vertex * 8 + k];
                uint16_t weight = geoset->skin[vertex * 8 + k + 4];
                if (!weight || group >= groupCount || groupOffsets[group] >= (uint32_t)geoset->num_matrices) continue;
                rawSkin[k] = R_AddGeosetMatrixPaletteEntry(geoset, geoset->matrices[groupOffsets[group]], &overflow);
                rawWeight[k] = (uint8_t)MIN(weight, 255);
            }
        } else {
            uint32_t group = geoset->vertexGroups ? (uint8_t)geoset->vertexGroups[vertex] : 0;
            uint32_t count = group < groupCount ? MIN(geoset->matrixGroupSizes[group], MAX_SKIN_BONES) : 0;
            uint32_t remaining = 255;
            FOR_LOOP(k, count) {
                if (groupOffsets[group] + k >= (uint32_t)geoset->num_matrices) break;
                rawSkin[k] = R_AddGeosetMatrixPaletteEntry(geoset, geoset->matrices[groupOffsets[group] + k], &overflow);
                rawWeight[k] = remaining / (count - k); remaining -= rawWeight[k];
            }
            if (!count) { rawSkin[0] = R_AddGeosetMatrixPaletteEntry(geoset, 0, &overflow); rawWeight[0] = 255; }
        }
        /* Insertion-sort top 4 (weight desc) into the 4-slot skin/boneWeight. */
        memset(vertices[vertex].skin, 0, 4);
        memset(vertices[vertex].boneWeight, 0, 4);
        FOR_LOOP(i, MAX_SKIN_BONES) {
            if (!rawWeight[i]) continue;
            FOR_LOOP(j, 4) {
                if (rawWeight[i] > vertices[vertex].boneWeight[j]) {
                    /* Shift lower entries down, dropping slot 3. */
                    for (int k = 3; k > (int)j; k--) {
                        vertices[vertex].skin[k] = vertices[vertex].skin[k - 1];
                        vertices[vertex].boneWeight[k] = vertices[vertex].boneWeight[k - 1];
                    }
                    vertices[vertex].skin[j] = rawSkin[i];
                    vertices[vertex].boneWeight[j] = rawWeight[i];
                    break;
                }
            }
        }
        /* Renormalize top-4 weights to sum=255. */
        uint32_t wsum = 0;
        FOR_LOOP(j, 4) wsum += vertices[vertex].boneWeight[j];
        if (wsum && wsum != 255) {
            uint32_t acc = 0;
            FOR_LOOP(j, 4) {
                uint32_t w = vertices[vertex].boneWeight[j] * 255 / wsum;
                vertices[vertex].boneWeight[j] = (uint8_t)w;
                acc += w;
            }
            /* Fix rounding remainder in the highest-weight slot. */
            if (acc < 255) vertices[vertex].boneWeight[0] += (uint8_t)(255 - acc);
        }
    }
    if (overflow.count) fprintf(stderr, "MDX palette overflow: %u references (first node %d)\n", overflow.count, overflow.first);
    ri.MemFree(groupOffsets);
}

/* Primary UVs stay in the shared packed vertex format; secondary channels occupy a compact separate stream. */
void MDX_PackModelGeometry(mdxModel_t *model, vertex_t *vertices, uint16_t *indices) {
    uint32_t base = 0, elems = 0;
    FOR_EACH_LIST(mdxGeoset_t, geo, model->geosets) {
        mdx_pack_vertices(geo, vertices + base);
        memcpy(indices + elems, geo->triangles, geo->num_triangles * sizeof(*indices));
        geo->indexofs = elems * sizeof(*indices);
        elems += geo->num_triangles; base += geo->num_vertices;
    }
}

/* Build compact model-owned GPU storage after all geoset blocks have been parsed. */
void MDX_BuildBuffers(mdxModel_t *model) {
    MDX_PartitionGeometry(model);
    static const struct { GLuint attr; GLint size; GLenum type; GLboolean norm; size_t ofs; } attrs[] = {
        { attrib_position, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, position) },
        { attrib_texcoord, 2, GL_FLOAT, GL_FALSE, offsetof(vertex_t, texcoord) },
        { attrib_normal, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, normal) },
        { attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(vertex_t, color) },
        { attrib_skin1, 4, GL_UNSIGNED_BYTE, GL_FALSE, offsetof(vertex_t, skin) },
        { attrib_boneWeight1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(vertex_t, boneWeight) },
    };
    uint32_t verts = 0, elems = 0, base = 0, uvcount = 0, uvbase = 0;
    FOR_EACH_LIST(mdxGeoset_t, geo, model->geosets) {
        verts += geo->num_vertices;
        elems += geo->num_triangles;
        uvcount += MAX(0, geo->num_texcoordChannels - 1) * geo->num_vertices;
    }
    if (!model->geosets) return;
    vertex_t *vertices = ri.MemAlloc(verts * sizeof(*vertices));
    uint16_t *indices = ri.MemAlloc(elems * sizeof(*indices));
    MDX_PackModelGeometry(model, vertices, indices);
    R_Call(glGenBuffers, uvcount ? BZ_MDX_BUFFER_COUNT : BZ_MDX_UV_BUFFER, model->buffers);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, model->buffers[BZ_MDX_VERTEX_BUFFER]);
    R_Call(glBufferData, GL_ARRAY_BUFFER, verts * sizeof(*vertices), vertices, GL_STATIC_DRAW);
    if (uvcount) {
        vec2_t *uvs = ri.MemAlloc(uvcount * sizeof(*uvs));
        FOR_EACH_LIST(mdxGeoset_t, geo, model->geosets) {
            uint32_t count = MAX(0, geo->num_texcoordChannels - 1) * geo->num_vertices;
            if (count) memcpy(uvs + uvbase, geo->texcoord + geo->num_texcoord, count * sizeof(*uvs));
            uvbase += count;
        }
        R_Call(glBindBuffer, GL_ARRAY_BUFFER, model->buffers[BZ_MDX_UV_BUFFER]);
        R_Call(glBufferData, GL_ARRAY_BUFFER, uvcount * sizeof(*uvs), uvs, GL_STATIC_DRAW);
        ri.MemFree(uvs);
    }
    uvbase = 0;
    base = 0;
    FOR_EACH_LIST(mdxGeoset_t, geo, model->geosets) {
        ARRAY_COUNT(geo->vaos) = MAX(1, geo->num_texcoordChannels);
        geo->vaos = ri.MemAlloc(ARRAY_COUNT(geo->vaos) * sizeof(*geo->vaos));
        R_Call(glGenVertexArrays, ARRAY_COUNT(geo->vaos), geo->vaos);
        FOR_LOOP(channel, ARRAY_COUNT(geo->vaos)) {
            R_Call(glBindVertexArray, geo->vaos[channel]);
            R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, model->buffers[BZ_MDX_INDEX_BUFFER]);
            R_Call(glBindBuffer, GL_ARRAY_BUFFER, model->buffers[BZ_MDX_VERTEX_BUFFER]);
            FOR_LOOP(i, sizeof(attrs) / sizeof(*attrs)) {
                R_Call(glEnableVertexAttribArray, attrs[i].attr);
                R_Call(glVertexAttribPointer, attrs[i].attr, attrs[i].size, attrs[i].type, attrs[i].norm, sizeof(vertex_t), (void *)(base * sizeof(vertex_t) + attrs[i].ofs));
            }
            if (channel) {
                R_Call(glBindBuffer, GL_ARRAY_BUFFER, model->buffers[BZ_MDX_UV_BUFFER]);
                R_Call(glVertexAttribPointer, attrib_texcoord, 2, GL_FLOAT, GL_FALSE, sizeof(vec2_t), (void *)((uintptr_t)(uvbase + (channel - 1) * geo->num_vertices) * sizeof(vec2_t)));
            }
        }
        uvbase += MAX(0, geo->num_texcoordChannels - 1) * geo->num_vertices;
        base += geo->num_vertices;
    }
    /* Upload once after binding a VAO: core GL owns the element-buffer binding in the VAO. */
    R_Call(glBufferData, GL_ELEMENT_ARRAY_BUFFER, elems * sizeof(*indices), indices, GL_STATIC_DRAW);
    ri.MemFree(indices); ri.MemFree(vertices);
}
