!!VP1.1
# Screen-space textured sprite pass. Sprite positions are already in pixels;
# z=0 and w=1 make them overlay the authored 3D scene without depth writes.
MOV R0.xy, v[0];
MOV R0.z, c[0].x;
MOV R0.w, c[0].y;
MOV o[HPOS], R0;
MOV o[COL0], v[3];
MOV o[TEX0], v[8];
END
