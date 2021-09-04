#ifndef baseTile_h_dsfg7d8f5
#define baseTile_h_dsfg7d8f5

#include <set>

struct TilePos
{
	sint32 x, y;

	TilePos() : x(0), y(0)
	{}

	bool operator < (const TilePos &other) const
	{
		if (y == other.y)
			return x < other.x;
		return y < other.y;
	}

	Real distanceToPlayer(Real tileLength) const
	{
		return distance(Vec2(x, y) * tileLength, Vec2(playerPosition));
	}
};

inline Stringizer &operator + (Stringizer &s, const TilePos &p)
{
	return s + p.x + " " + p.y;
}

std::set<TilePos> findNeededTiles(Real tileLength, Real range);

#endif // !baseTile_h_dsfg7d8f5
