// Frozen production surface collector from 30451bf (GPL-2.0-or-later).
static void R_BoxSurfaces_r( mnode_t *node, vec3_t mins, vec3_t maxs, surfaceType_t **list, int listsize, int *listlength, vec3_t dir ) {

	int			s, c;
	msurface_t	*surf, **mark;

	// do the tail recursion in a loop
	while ( node->contents == -1 ) {
		s = BoxOnPlaneSide( mins, maxs, node->plane );
		if (s == 1) {
			node = node->children[0];
		} else if (s == 2) {
			node = node->children[1];
		} else {
			R_BoxSurfaces_r(node->children[0], mins, maxs, list, listsize, listlength, dir);
			node = node->children[1];
		}
	}

	// add the individual surfaces
	mark = node->firstmarksurface;
	c = node->nummarksurfaces;
	while (c--) {
		//
		if (*listlength >= listsize) break;
		//
		surf = *mark;
		// check if the surface has NOIMPACT or NOMARKS set
		if ( ( surf->shader->surfaceFlags & ( SURF_NOIMPACT | SURF_NOMARKS ) )
			|| ( surf->shader->contentFlags & CONTENTS_FOG ) ) {
			surf->viewCount = tr.viewCount;
		}
		// extra check for surfaces to avoid list overflows
		else if (*(surf->data) == SF_FACE) {
			// the face plane should go through the box
			s = BoxOnPlaneSide( mins, maxs, &(( srfSurfaceFace_t * ) surf->data)->plane );
			if (s == 1 || s == 2) {
				surf->viewCount = tr.viewCount;
			} else if (DotProduct((( srfSurfaceFace_t * ) surf->data)->plane.normal, dir) > -0.5) {
			// don't add faces that make sharp angles with the projection direction
				surf->viewCount = tr.viewCount;
			}
		}
		else if (*(surfaceType_t *) (surf->data) != SF_GRID &&
			 *(surfaceType_t *) (surf->data) != SF_TRIANGLES)
			surf->viewCount = tr.viewCount;
		// check the viewCount because the surface may have
		// already been added if it spans multiple leafs
		if (surf->viewCount != tr.viewCount) {
			surf->viewCount = tr.viewCount;
			list[*listlength] = (surfaceType_t *) surf->data;
			(*listlength)++;
		}
		mark++;
	}
}
