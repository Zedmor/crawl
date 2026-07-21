#include "AppHdr.h"

#include "viewgeom.h"

#include "dungeon.h"
#include "end.h"
#include "options.h"
#include "output.h"
#include "state.h"
#include "tiles-build-specific.h"


// ----------------------------------------------------------------------
// Layout helper classes
// ----------------------------------------------------------------------

// Moved from directn.h, where they didn't need to be.
// define VIEW_MIN_HEIGHT defined elsewhere
// define VIEW_MAX_HEIGHT use Options.view_max_height
// define VIEW_MIN_WIDTH defined elsewhere
// define VIEW_MAX_WIDTH use Options.view_max_width
#define HUD_WIDTH  42
#define HUD_HEIGHT 13
#define MSG_MAX_HEIGHT Options.msg_max_height
#define MLIST_MIN_HEIGHT Options.mlist_min_height
#define MLIST_MIN_WIDTH 25  // non-inline layout only
#define MLIST_MAX_WIDTH 42
// The panel sidebar is capped so it never hogs a wide terminal; any width it
// leaves goes to the map, which is the focus. Two columns need ~42 (left) + 1
// (gutter) + ~45 (right).
#define SIDEBAR_MAX_WIDTH 90
#define MLIST_GUTTER 1
#define HUD_MIN_GUTTER 2
#define HUD_MAX_GUTTER 4

// Helper for layouts. Tries to increment lvalue without overflowing it.
static void _increment(int& lvalue, int delta, int max_value)
{
    lvalue = min(lvalue+delta, max_value);
}

class _layout
{
public:
    _layout(coord_def termsz_, coord_def hudsz_) :
        termp(1,1),    termsz(termsz_),
        viewp(-1,-1),  viewsz(VIEW_MIN_WIDTH, VIEW_MIN_HEIGHT),
        hudp(-1,-1),   hudsz(hudsz_),
        msgp(-1,-1),   msgsz(0, Options.msg_min_height),
        mlistp(-1,-1), mlistsz(MLIST_MIN_WIDTH, 0),
        hud_gutter(HUD_MIN_GUTTER),
        valid(false) {}

 protected:
// Smart compilers can recognize some of these assertions as tautological,
// but we do want to keep them in case something changes.
// A discussion: http://kerneltrap.org/node/7434
#ifdef TARGET_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wstrict-overflow"
#endif
    void _assert_validity() const
    {
#ifndef USE_TILE_LOCAL
        ASSERT(viewp.x >= 1);
        ASSERT(viewp.y >= 1);
        // Check that all the panes fit in the view.
        ASSERT((viewp+viewsz - termp).x <= termsz.x);
        ASSERT((viewp+viewsz - termp).y <= termsz.y);

        ASSERT((hudp+hudsz - termp).x <= termsz.x);
        ASSERT((hudp+hudsz - termp).y <= termsz.y);

        ASSERT((msgp+msgsz - termp).x <= termsz.x);
        ASSERT((msgp+msgsz - termp).y <= termsz.y);

        ASSERT((mlistp+mlistsz-termp).x <= termsz.x);
        ASSERT((mlistp+mlistsz-termp).y <= termsz.y);
#endif
    }
public:
    const coord_def termp, termsz;
    coord_def viewp, viewsz;
    coord_def hudp;
    const coord_def hudsz;
    coord_def msgp, msgsz;
    coord_def mlistp, mlistsz;
    int hud_gutter;
    bool valid;
};

// vvvvvvghhh  v=view, g=hud gutter, h=hud, l=list, m=msg
// vvvvvvghhh
// vvvvvv lll
//        lll
// mmmmmmmmmm
class _inline_layout : public _layout
{
public:
    _inline_layout(coord_def termsz_, coord_def hudsz_) :
        _layout(termsz_, hudsz_)
    {
        valid = _init();
    }

    bool _init()
    {
        // x: View gets leftover; then mlist; then hud gutter
        if (leftover_x() < 0)
            return false;

        _increment(viewsz.x,   leftover_x(), Options.view_max_width);

        if ((viewsz.x % 2) != 1)
            --viewsz.x;

        mlistsz.x = hudsz.x;
        _increment(mlistsz.x,  leftover_x(), MLIST_MAX_WIDTH);
        _increment(hud_gutter, leftover_x(), HUD_MAX_GUTTER);
        // Cap the sidebar (was unbounded, which let it eat the whole terminal),
        // then hand any remaining width to the map so it stays the focus.
        _increment(mlistsz.x,  leftover_x(), SIDEBAR_MAX_WIDTH);
        _increment(viewsz.x,   leftover_x(), GXM);
        if ((viewsz.x % 2) != 1)
            --viewsz.x;
        msgsz.x = termsz.x;

        // y: View gets as much as it wants.
        // mlist tries to get at least its minimum.
        // msg expands as much as it wants.
        // mlist gets any leftovers.
        if (leftover_y() < 0)
            return false;

        _increment(viewsz.y, leftover_leftcol_y(), Options.view_max_height);
        if ((viewsz.y % 2) != 1)
            --viewsz.y;

        if (mlistsz.y < MLIST_MIN_HEIGHT)
            _increment(mlistsz.y, leftover_rightcol_y(), MLIST_MIN_HEIGHT);
        _increment(msgsz.y,  leftover_y(), MSG_MAX_HEIGHT);
        // Let the map fill the space beneath it (down to the message pane), so
        // tall terminals spend that room on the game field instead of leaving
        // it blank. The sidebar then fills the right column independently.
        _increment(viewsz.y, leftover_leftcol_y(), GYM);
        if ((viewsz.y % 2) != 1)
            --viewsz.y;
        _increment(mlistsz.y, leftover_rightcol_y(), INT_MAX);

        // Finish off by doing the positions.
        if (Options.messages_at_top)
        {
            msgp = termp;
            viewp = termp + coord_def(0, msgsz.y);
        }
        else
        {
            viewp  = termp;
            msgp   = termp + coord_def(0, max(viewsz.y, hudsz.y+mlistsz.y));
        }
        hudp   = viewp + coord_def(viewsz.x+hud_gutter, 0);
        mlistp = hudp  + coord_def(0, hudsz.y);

        _assert_validity();
        return true;
    }

    int leftover_x() const
    {
        int width = (viewsz.x + hud_gutter + max(hudsz.x, mlistsz.x));
        return termsz.x - width;
    }
    int leftover_rightcol_y() const { return termsz.y-hudsz.y-mlistsz.y-msgsz.y; }
    int leftover_leftcol_y() const  { return termsz.y-viewsz.y-msgsz.y; }
    int leftover_y() const
    {
        return min(leftover_rightcol_y(), leftover_leftcol_y());
    }
};

// ll vvvvvvghhh  v=view, g=hud gutter, h=hud, l=list, m=msg
// ll vvvvvvghhh
// ll vvvvvv
// mmmmmmmmmmmmm
class _mlist_col_layout : public _layout
{
public:
    _mlist_col_layout(coord_def termsz_, coord_def hudsz_)
        : _layout(termsz_, hudsz_)
    { valid = _init(); }
    bool _init()
    {
        // Don't let the mlist column steal all the width. Up front,
        // take some for the view. If it makes the layout fail, that's fine.
        _increment(viewsz.x, MLIST_MIN_WIDTH/2, Options.view_max_width);

        // x: View and mlist share leftover; then hud gutter.
        if (leftover_x() < 0)
            return false;

        _increment(mlistsz.x,  leftover_x()/2, MLIST_MAX_WIDTH);
        _increment(viewsz.x,   leftover_x(),   Options.view_max_width);

        if ((viewsz.x % 2) != 1)
            --viewsz.x;

        _increment(mlistsz.x,  leftover_x(),   MLIST_MAX_WIDTH);
        _increment(hud_gutter, leftover_x(),   HUD_MAX_GUTTER);
        msgsz.x = termsz.x-1; // Can't use last character.

        // y: View gets leftover; then message.
        if (leftover_y() < 0)
            return false;

        _increment(viewsz.y, leftover_y(), Options.view_max_height);

        if ((viewsz.y % 2) != 1)
            --viewsz.y;

        _increment(msgsz.y,  leftover_y(), INT_MAX);
        mlistsz.y = viewsz.y;

        // Finish off by doing the positions.
        mlistp = termp;
        viewp  = mlistp+ coord_def(mlistsz.x+MLIST_GUTTER, 0);
        msgp   = termp + coord_def(0, viewsz.y);
        hudp   = viewp + coord_def(viewsz.x+hud_gutter, 0);

        _assert_validity();
        return true;
    }
 private:
    int leftover_x() const
    {
        int width = (mlistsz.x + MLIST_GUTTER + viewsz.x + hud_gutter + hudsz.x);
        return termsz.x - width;
    }
    int leftover_y() const
    {
        const int top_y = max(max(viewsz.y, hudsz.y), mlistsz.y);
        const int height = top_y + msgsz.y;
        return termsz.y - height;
    }
};

//////////////////////////////////////////////////////////////////////////////
// crawl_view_buffer

crawl_view_buffer::crawl_view_buffer()
    : m_size(0, 0)
    , m_buffer(nullptr)
{
}
crawl_view_buffer::crawl_view_buffer(const coord_def &sz)
    : m_size(0, 0)
    , m_buffer(nullptr)
{
    resize(sz);
}

crawl_view_buffer::~crawl_view_buffer()
{
    delete [] m_buffer;
}

void crawl_view_buffer::resize(const coord_def &sz)
{
    delete [] m_buffer;
    m_size = sz;
    m_buffer = new screen_cell_t [ sz.x * sz.y ];
}

bool crawl_view_buffer::empty() const
{
    return m_size.x * m_size.y <= 0;
}

crawl_view_buffer::crawl_view_buffer(const crawl_view_buffer &rhs)
    : crawl_view_buffer(rhs.m_size)
{
    if (rhs.m_buffer)
    {
        size_t count = m_size.x * m_size.y;
        copy(rhs.m_buffer, rhs.m_buffer+count, m_buffer);
    }
}

const crawl_view_buffer &crawl_view_buffer::operator = (crawl_view_buffer rhs)
{
    swap(m_size, rhs.m_size);
    swap(m_buffer, rhs.m_buffer);
    return *this;
}

void crawl_view_buffer::fill(const screen_cell_t& value)
{
    for (int i = 0; i < m_size.x * m_size.y; ++i)
        m_buffer[i] = value;
}

void crawl_view_buffer::clear()
{
    delete [] m_buffer;
    m_buffer = nullptr;
    m_size = coord_def(0,0);
}

// ----------------------------------------------------------------------
// crawl_view_geometry
// ----------------------------------------------------------------------

crawl_view_geometry::crawl_view_geometry()
    : termp(1, 1), termsz(80, 24),
      viewp(1, 1), viewsz(VIEW_BASE_WIDTH, 17),
      hudp(40, 1), hudsz(-1, -1),
      msgp(1, viewp.y + viewsz.y), msgsz(80, 7),
      mlistp(hudp.x, hudp.y + hudsz.y),
      mlistsz(hudsz.x, msgp.y - mlistp.y),
      vgrdc(), viewhalfsz(), glos1(), glos2(),
      vlos1(), vlos2(), mousep(), last_player_pos()
{
}

void crawl_view_geometry::init_view()
{
    viewhalfsz = viewsz / 2;
    if (!crawl_state.game_is_arena())
        set_player_at(you.pos(), true);
    else
    {
        coord_def yplace(dgn_find_feature_marker(DNGN_ESCAPE_HATCH_UP));
        crawl_view.set_player_at(yplace);
    }
}

void crawl_view_geometry::shift_player_to(const coord_def &c)
{
    // Preserve vgrdc offset after moving.
    const coord_def offset = crawl_view.vgrdc - last_player_pos;
    crawl_view.vgrdc = offset + c;
    last_player_pos = c;

    set_player_at(c);

    ASSERT(crawl_view.vgrdc == offset + c);
    ASSERT(last_player_pos == c);
}

void crawl_view_geometry::set_player_at(const coord_def &c, bool centre)
{
    if (centre)
        vgrdc = c;
    else
    {
        const coord_def oldc = vgrdc;
        const int xmarg = Options.scroll_margin_x + LOS_RADIUS <= viewhalfsz.x
                            ? Options.scroll_margin_x
                            : viewhalfsz.x - LOS_RADIUS;
        const int ymarg = Options.scroll_margin_y + LOS_RADIUS <= viewhalfsz.y
                            ? Options.scroll_margin_y
                            : viewhalfsz.y - LOS_RADIUS;

        if (Options.view_lock_x)
            vgrdc.x = c.x;
        else if (c.x - LOS_RADIUS < vgrdc.x - viewhalfsz.x + xmarg)
            vgrdc.x = c.x - LOS_RADIUS + viewhalfsz.x - xmarg;
        else if (c.x + LOS_RADIUS > vgrdc.x + viewhalfsz.x - xmarg)
            vgrdc.x = c.x + LOS_RADIUS - viewhalfsz.x + xmarg;

        if (Options.view_lock_y)
            vgrdc.y = c.y;
        else if (c.y - LOS_RADIUS < vgrdc.y - viewhalfsz.y + ymarg)
            vgrdc.y = c.y - LOS_RADIUS + viewhalfsz.y - ymarg;
        else if (c.y + LOS_RADIUS > vgrdc.y + viewhalfsz.y - ymarg)
            vgrdc.y = c.y + LOS_RADIUS - viewhalfsz.y + ymarg;

        if (vgrdc != oldc && Options.centre_on_scroll)
            vgrdc = c;

        if (!Options.centre_on_scroll && Options.symmetric_scroll
            && !Options.view_lock_x
            && !Options.view_lock_y
            && (c - last_player_pos).abs() == 2
            && (vgrdc - oldc).abs() == 1)
        {
            const coord_def dp = c - last_player_pos;
            const coord_def dc = vgrdc - oldc;
            if ((dc.x == dp.x) != (dc.y == dp.y))
                vgrdc = oldc + dp;
        }
    }

    glos1 = c - coord_def(LOS_RADIUS, LOS_RADIUS);
    glos2 = c + coord_def(LOS_RADIUS, LOS_RADIUS);

    calc_vlos();

    last_player_pos = c;
}

void crawl_view_geometry::init_geometry()
{
    termsz = coord_def(get_number_of_cols(), get_number_of_lines());

    // currently, webtiles has weird interactions with this logic (I think
    // because of extra resize calls). But this is basically safe because
    // dgamelaunch wraps terminal size and prevents smallterm.
#ifndef USE_TILE_LOCAL
    const bool smallterm = termsz.x < MIN_COLS || termsz.y < MIN_LINES;
    crawl_state.smallterm = smallterm;
    termsz.x = max(termsz.x, MIN_COLS);
    termsz.y = max(termsz.y, MIN_LINES);
#endif
    hudsz  = coord_def(HUD_WIDTH, HUD_HEIGHT);

    const _inline_layout lay_inline(termsz, hudsz);
    const _mlist_col_layout lay_mlist(termsz, hudsz);

    const _layout* winner = &lay_inline;
    if (Options.mlist_allow_alternate_layout
        && lay_mlist.valid)
    {
        winner = &lay_mlist;
    }
#ifndef USE_TILE_LOCAL
    // I don't know why this crashes on local tiles
    ASSERT(winner->valid);
#endif

    msgp    = winner->msgp;
    msgsz   = winner->msgsz;
    viewp   = winner->viewp;
    viewsz  = winner->viewsz;
    hudp    = winner->hudp;
    hudsz   = winner->hudsz;
    mlistp  = winner->mlistp;
    mlistsz = winner->mlistsz;

    // Carve an always-on inventory panel out of the bottom of the monster-list
    // column, when enabled and there is enough vertical room. This is the
    // Cogmind-style "persistent side panel": larger terminals yield a taller
    // panel (more of the pack visible at once), while small terminals keep the
    // classic layout untouched (graceful degradation). Items past the panel's
    // height are summarised with a (…) marker, exactly like the monster list.
    invp    = coord_def(0, 0);
    invsz   = coord_def(0, 0);
    minfp   = coord_def(0, 0);
    minfsz  = coord_def(0, 0);
    abilp   = coord_def(0, 0);
    abilsz  = coord_def(0, 0);
    skillp  = coord_def(0, 0);
    skillsz = coord_def(0, 0);
    spellp  = coord_def(0, 0);
    spellsz = coord_def(0, 0);
#ifndef USE_TILE_LOCAL
    // Only the default (inline) layout stacks the monster list directly below
    // the HUD, which is the space these sidebar panels are carved from. When
    // the sidebar is wide enough we split it into two columns: the left column
    // (Monsters / Monster inspector / Inventory) sits under the stats, and a
    // right column (Abilities / Skills) fills the space beside the stats that
    // was otherwise blank. Narrow terminals fall back to a single stacked
    // column, and Ctrl-V (or a small terminal) drops back to the classic view.
    if (Options.show_inventory_panel && !ui_panels_hidden() && winner == &lay_inline)
    {
        const int mheader   = 1;        // "Monsters" title rule above the list
        const int left_w    = hudsz.x;  // stats/HUD width; the left column width
        const int gutter    = 1;
        const int sidebar_w = mlistsz.x;
        const int mlist_reserve = 5;
        const int inv_min   = 6;
        const int inv_max   = 54;
        const int minfo_want = 12;
        const bool two_col  = sidebar_w >= left_w + gutter + 33;

        if (two_col)
        {
            // Right column beside the stats: Monster inspector on top (prime
            // combat real-estate and guaranteed room here, since this column
            // uses the full height), then Abilities, Skills, Spells. The
            // enabled panels split the height evenly; the last gets the
            // remainder so the column reaches the message pane.
            const int right_x = hudp.x + left_w + gutter;
            const int right_w = sidebar_w - left_w - gutter;
            const int top     = hudp.y;
            const int right_h = msgp.y - top;
            const bool wi  = Options.show_monster_info_panel;
            const bool wa  = Options.show_abilities_panel;
            const bool ws  = Options.show_skills_panel;
            const bool wsp = Options.show_spells_panel;
            int n = (wi ? 1 : 0) + (wa ? 1 : 0) + (ws ? 1 : 0) + (wsp ? 1 : 0);
            if (n > 0 && right_h >= 2)
            {
                const int each = right_h / n;
                int ry = top;
                int remaining = n;
                if (wi)
                {
                    const int h = (remaining == 1) ? (msgp.y - ry) : each;
                    minfp = coord_def(right_x, ry);
                    minfsz = coord_def(right_w, h);
                    ry += h; --remaining;
                }
                if (wa)
                {
                    const int h = (remaining == 1) ? (msgp.y - ry) : each;
                    abilp = coord_def(right_x, ry);
                    abilsz = coord_def(right_w, h);
                    ry += h; --remaining;
                }
                if (ws)
                {
                    const int h = (remaining == 1) ? (msgp.y - ry) : each;
                    skillp = coord_def(right_x, ry);
                    skillsz = coord_def(right_w, h);
                    ry += h; --remaining;
                }
                if (wsp)
                {
                    const int h = (remaining == 1) ? (msgp.y - ry) : each;
                    spellp = coord_def(right_x, ry);
                    spellsz = coord_def(right_w, h);
                    ry += h; --remaining;
                }
            }

            // Left column under the stats: just Monsters list + Inventory
            // (the inspector lives in the right column, which has more room).
            mlistsz.x = left_w;
            const int avail = mlistsz.y - mheader;
            if (avail >= mlist_reserve + inv_min)
            {
                int inv_h = min(avail - mlist_reserve, inv_max);
                inv_h = max(inv_h, inv_min);
                const int mlist_h = avail - inv_h;
                mlistp.y += mheader;
                mlistsz.y = mlist_h;
                invp  = coord_def(mlistp.x, mlistp.y + mlist_h);
                invsz = coord_def(left_w, inv_h);
            }
        }
        else
        {
            // Narrow: a single stacked column, panels added in priority order.
            const int abil_want  = 8;
            const int skill_want = 8;
            const int spell_want = 8;
            const int avail = mlistsz.y - mheader;
            if (avail >= mlist_reserve + inv_min)
            {
                int budget = avail - mlist_reserve - inv_min;
                int minfo_h = 0, abil_h = 0, skill_h = 0, spell_h = 0;
                if (Options.show_monster_info_panel && budget >= minfo_want)
                    { minfo_h = minfo_want; budget -= minfo_want; }
                if (Options.show_abilities_panel && budget >= abil_want)
                    { abil_h = abil_want; budget -= abil_want; }
                if (Options.show_skills_panel && budget >= skill_want)
                    { skill_h = skill_want; budget -= skill_want; }
                if (Options.show_spells_panel && budget >= spell_want)
                    { spell_h = spell_want; budget -= spell_want; }

                const int optional = minfo_h + abil_h + skill_h + spell_h;
                int inv_h = min(avail - mlist_reserve - optional, inv_max);
                inv_h = max(inv_h, inv_min);
                const int mlist_h = avail - optional - inv_h;

                mlistp.y += mheader;
                mlistsz.y = mlist_h;
                int y = mlistp.y + mlist_h;
                if (minfo_h > 0)
                    { minfp = coord_def(mlistp.x, y); minfsz = coord_def(mlistsz.x, minfo_h); y += minfo_h; }
                if (abil_h > 0)
                    { abilp = coord_def(mlistp.x, y); abilsz = coord_def(mlistsz.x, abil_h); y += abil_h; }
                if (skill_h > 0)
                    { skillp = coord_def(mlistp.x, y); skillsz = coord_def(mlistsz.x, skill_h); y += skill_h; }
                if (spell_h > 0)
                    { spellp = coord_def(mlistp.x, y); spellsz = coord_def(mlistsz.x, spell_h); y += spell_h; }
                invp  = coord_def(mlistp.x, y);
                invsz = coord_def(mlistsz.x, inv_h);
            }
        }
    }
#endif

#ifdef USE_TILE_LOCAL
    // libgui may redefine these based on its own settings.
    gui_init_view_params(*this);
#endif
#ifdef USE_TILE_WEB
    tiles.layout_reset();
#endif

#ifdef USE_TILE
    tiles.resize();
#endif

    init_view();
    return;
}

void crawl_view_geometry::calc_vlos()
{
    vlos1 = grid2view(glos1);
    vlos2 = grid2view(glos2);
}
