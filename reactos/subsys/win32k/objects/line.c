/*
 *  ReactOS W32 Subsystem
 *  Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/* $Id: line.c,v 1.19 2003/08/17 17:32:58 royce Exp $ */

// Some code from the WINE project source (www.winehq.com)

#undef WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <internal/safe.h>
#include <ddk/ntddk.h>
#include <win32k/dc.h>
#include <win32k/line.h>
#include <win32k/path.h>
#include <win32k/pen.h>
#include <win32k/region.h>
#include <include/error.h>
#include <include/inteng.h>
#include <include/object.h>
#include <include/path.h>

#define NDEBUG
#include <win32k/debug1.h>


BOOL
STDCALL
W32kAngleArc(HDC  hDC,
             int  X,
             int  Y,
             DWORD  Radius,
             FLOAT  StartAngle,
             FLOAT  SweepAngle)
{
  UNIMPLEMENTED;
}

BOOL
STDCALL
W32kArc(HDC  hDC,
        int  LeftRect,
        int  TopRect,
        int  RightRect,
        int  BottomRect,
        int  XStartArc,
        int  YStartArc,
        int  XEndArc,
        int  YEndArc)
{
  DC *dc = DC_HandleToPtr(hDC);
  if(!dc) return FALSE;

  if(PATH_IsPathOpen(dc->w.path))
  {
    DC_ReleasePtr ( hDC );
    return PATH_Arc(hDC, LeftRect, TopRect, RightRect, BottomRect,
                    XStartArc, YStartArc, XEndArc, YEndArc);
  }

  // FIXME
//   EngArc(dc, LeftRect, TopRect, RightRect, BottomRect, UNIMPLEMENTED
//          XStartArc, YStartArc, XEndArc, YEndArc);

  DC_ReleasePtr( hDC );
  return TRUE;
}

BOOL
STDCALL
W32kArcTo(HDC  hDC,
          int  LeftRect,
          int  TopRect,
          int  RightRect,
          int  BottomRect,
          int  XRadial1,
          int  YRadial1,
          int  XRadial2,
          int  YRadial2)
{
  BOOL result;
  //DC *dc;

  // Line from current position to starting point of arc
  if ( !W32kLineTo(hDC, XRadial1, YRadial1) )
    return FALSE;

  //dc = DC_HandleToPtr(hDC);

  //if(!dc) return FALSE;

  // Then the arc is drawn.
  result = W32kArc(hDC, LeftRect, TopRect, RightRect, BottomRect,
                   XRadial1, YRadial1, XRadial2, YRadial2);

  //DC_ReleasePtr( hDC );

  // If no error occured, the current position is moved to the ending point of the arc.
  if(result)
    W32kMoveToEx(hDC, XRadial2, YRadial2, NULL);

  return result;
}

INT
FASTCALL
IntGetArcDirection ( PDC dc )
{
  ASSERT ( dc );
  return dc->w.ArcDirection;
}

INT
STDCALL
W32kGetArcDirection(HDC  hDC)
{
  PDC dc = DC_HandleToPtr (hDC);
  int ret = 0; // default to failure

  if ( dc )
  {
    ret = IntGetArcDirection ( dc );
    DC_ReleasePtr( hDC );
  }

  return ret;
}

BOOL
STDCALL
W32kLineTo(HDC  hDC,
           int  XEnd,
           int  YEnd)
{
  DC      *dc = DC_HandleToPtr(hDC);
  SURFOBJ *SurfObj;
  BOOL     Ret;
  BRUSHOBJ PenBrushObj;
  RECT     Bounds;

  if ( !dc )
    {
      SetLastWin32Error(ERROR_INVALID_HANDLE);
      return FALSE;
    }

  SurfObj = (SURFOBJ*)AccessUserObject ( (ULONG)dc->Surface );

  if (PATH_IsPathOpen(dc->w.path))
    {
      DC_ReleasePtr(hDC);
      Ret = PATH_LineTo(hDC, XEnd, YEnd);
      if (Ret)
	{
	  // FIXME - PATH_LineTo should maybe do this...
	  dc = DC_HandleToPtr(hDC);
	  dc->w.CursPosX = XEnd;
	  dc->w.CursPosY = YEnd;
	  DC_ReleasePtr(hDC);
	}
      return Ret;
    }
  else
    {
      if (dc->w.CursPosX <= XEnd)
	{
	  Bounds.left = dc->w.CursPosX;
	  Bounds.right = XEnd;
	}
      else
	{
	  Bounds.left = XEnd;
	  Bounds.right = dc->w.CursPosX;
	}
      Bounds.left += dc->w.DCOrgX;
      Bounds.right += dc->w.DCOrgX;
      if (dc->w.CursPosY <= YEnd)
	{
	  Bounds.top = dc->w.CursPosY;
	  Bounds.bottom = YEnd;
	}
      else
	{
	  Bounds.top = YEnd;
	  Bounds.bottom = dc->w.CursPosY;
	}
      Bounds.top += dc->w.DCOrgY;
      Bounds.bottom += dc->w.DCOrgY;

      /* make BRUSHOBJ from current pen. */
      HPenToBrushObj ( &PenBrushObj, dc->w.hPen );

      Ret = IntEngLineTo(SurfObj,
                         dc->CombinedClip,
                         &PenBrushObj,
                         dc->w.DCOrgX + dc->w.CursPosX, dc->w.DCOrgY + dc->w.CursPosY,
                         dc->w.DCOrgX + XEnd,           dc->w.DCOrgY + YEnd,
                         &Bounds,
                         dc->w.ROPmode);
    }

  if (Ret)
    {
      dc->w.CursPosX = XEnd;
      dc->w.CursPosY = YEnd;
    }
  DC_ReleasePtr(hDC);

  return Ret;
}

BOOL
STDCALL
W32kMoveToEx(HDC      hDC,
             int      X,
             int      Y,
             LPPOINT  Point)
{
  DC   *dc = DC_HandleToPtr( hDC );
  BOOL  PathIsOpen;

  if ( !dc ) return FALSE;

  if ( Point )
  {
    Point->x = dc->w.CursPosX;
    Point->y = dc->w.CursPosY;
  }
  dc->w.CursPosX = X;
  dc->w.CursPosY = Y;

  PathIsOpen = PATH_IsPathOpen(dc->w.path);

  DC_ReleasePtr ( hDC );

  if ( PathIsOpen )
    return PATH_MoveTo ( hDC );

  return TRUE;
}

BOOL
STDCALL
W32kPolyBezier(HDC            hDC,
               CONST LPPOINT  pt,
               DWORD          Count)
{
  DC *dc = DC_HandleToPtr(hDC);
  BOOL ret = FALSE; // default to FAILURE

  if ( !dc ) return FALSE;

  if ( PATH_IsPathOpen(dc->w.path) )
  {
    DC_ReleasePtr( hDC );
    return PATH_PolyBezier ( hDC, pt, Count );
  }

  /* We'll convert it into line segments and draw them using Polyline */
  {
    POINT *Pts;
    INT nOut;

    Pts = GDI_Bezier ( pt, Count, &nOut );
    if ( Pts )
    {
      DbgPrint("Pts = %p, no = %d\n", Pts, nOut);
      ret = W32kPolyline(dc->hSelf, Pts, nOut);
      ExFreePool(Pts);
    }
  }
  DC_ReleasePtr( hDC );
  return ret;
}

BOOL
STDCALL
W32kPolyBezierTo(HDC  hDC,
                 CONST LPPOINT  pt,
                 DWORD  Count)
{
  DC *dc = DC_HandleToPtr(hDC);
  BOOL ret = FALSE; // default to failure

  if ( !dc ) return ret;

  if ( PATH_IsPathOpen(dc->w.path) )
    ret = PATH_PolyBezierTo ( hDC, pt, Count );
  else /* We'll do it using PolyBezier */
  {
    POINT *npt;
    npt = ExAllocatePool(NonPagedPool, sizeof(POINT) * (Count + 1));
    if ( npt )
    {
      npt[0].x = dc->w.CursPosX;
      npt[0].y = dc->w.CursPosY;
      memcpy(npt + 1, pt, sizeof(POINT) * Count);
      ret = W32kPolyBezier(dc->hSelf, npt, Count+1);
      ExFreePool(npt);
    }
  }
  if ( ret )
  {
    dc->w.CursPosX = pt[Count-1].x;
    dc->w.CursPosY = pt[Count-1].y;
  }
  DC_ReleasePtr( hDC );
  return ret;
}

BOOL
STDCALL
W32kPolyDraw(HDC            hDC,
             CONST LPPOINT  pt,
             CONST LPBYTE   Types,
             int            Count)
{
  UNIMPLEMENTED;
}

BOOL
FASTCALL
IntPolyline(PDC           dc,
            CONST LPPOINT pt,
            int           Count)
{
  SURFOBJ     *SurfObj = NULL;
  BOOL         ret = FALSE; // default to failure
  LONG         i;
  PROSRGNDATA  reg;
  BRUSHOBJ     PenBrushObj;
  POINT       *pts;

  SurfObj = (SURFOBJ*)AccessUserObject((ULONG)dc->Surface);
  ASSERT(SurfObj);

  if ( PATH_IsPathOpen ( dc->w.path ) )
    return PATH_Polyline ( dc, pt, Count );

  reg = (PROSRGNDATA)GDIOBJ_LockObj(dc->w.hGCClipRgn, GO_REGION_MAGIC);

  //FIXME: Do somthing with reg...

  //Allocate "Count" bytes of memory to hold a safe copy of pt
  pts = (POINT*)ExAllocatePool ( NonPagedPool, sizeof(POINT)*Count );
  if ( pts )
  {
    // safely copy pt to local version
    if ( STATUS_SUCCESS == MmCopyFromCaller(pts, pt, sizeof(POINT)*Count) )
    {
      //offset the array of point by the dc->w.DCOrg
      for ( i = 0; i < Count; i++ )
      {
	pts[i].x += dc->w.DCOrgX;
	pts[i].y += dc->w.DCOrgY;
      }

      /* make BRUSHOBJ from current pen. */
      HPenToBrushObj ( &PenBrushObj, dc->w.hPen );

      //get IntEngPolyline to do the drawing.
      ret = IntEngPolyline(SurfObj,
			   dc->CombinedClip,
			   &PenBrushObj,
			   pts,
			   Count,
			   dc->w.ROPmode);
    }

    ExFreePool ( pts );
  }

  //Clean up
  GDIOBJ_UnlockObj ( dc->w.hGCClipRgn, GO_REGION_MAGIC );

  return ret;
}

BOOL
STDCALL
W32kPolyline(HDC            hDC,
             CONST LPPOINT  pt,
             int            Count)
{
  DC    *dc = DC_HandleToPtr(hDC);
  BOOL   ret = FALSE; // default to failure

  if ( dc )
  {
    ret = IntPolyline ( dc, pt, Count );

    DC_ReleasePtr( hDC );
  }

  return ret;
}

BOOL
STDCALL
W32kPolylineTo(HDC            hDC,
               CONST LPPOINT  pt,
               DWORD          Count)
{
  DC *dc = DC_HandleToPtr(hDC);
  BOOL ret = FALSE; // default to failure

  if ( !dc ) return ret;

  if(PATH_IsPathOpen(dc->w.path))
  {
    ret = PATH_PolylineTo(hDC, pt, Count);
  }
  else /* do it using Polyline */
  {
    POINT *pts = ExAllocatePool(NonPagedPool, sizeof(POINT) * (Count + 1));
    if ( pts )
    {
      pts[0].x = dc->w.CursPosX;
      pts[0].y = dc->w.CursPosY;
      memcpy( pts + 1, pt, sizeof(POINT) * Count);
      ret = W32kPolyline(hDC, pts, Count + 1);
      ExFreePool(pts);
    }
  }
  if ( ret )
  {
    dc->w.CursPosX = pt[Count-1].x;
    dc->w.CursPosY = pt[Count-1].y;
  }
  DC_ReleasePtr( hDC );
  return ret;
}

BOOL
STDCALL
W32kPolyPolyline(HDC            hDC,
                 CONST LPPOINT  pt,
                 CONST LPDWORD  PolyPoints,
                 DWORD          Count)
{
   UNIMPLEMENTED;
}

int
STDCALL
W32kSetArcDirection(HDC  hDC,
                    int  ArcDirection)
{
  PDC  dc;
  INT  nOldDirection = 0; // default to FAILURE

  dc = DC_HandleToPtr (hDC);
  if ( !dc ) return 0;

  if ( ArcDirection == AD_COUNTERCLOCKWISE || ArcDirection == AD_CLOCKWISE )
  {
    nOldDirection = dc->w.ArcDirection;
    dc->w.ArcDirection = ArcDirection;
  }

  DC_ReleasePtr( hDC );
  return nOldDirection;
}
/* EOF */
