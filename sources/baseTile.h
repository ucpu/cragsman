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

	real distanceToPlayer(real tileLength) const
	{
		return distance(vec2(x, y) * tileLength, vec2(playerPosition));
	}
};

inline stringizer &operator + (stringizer &s, const TilePos &p)
{
	return s + p.x + " " + p.y;
}

std::set<TilePos> findNeededTiles(real tileLength, real range);

#endif // !baseTile_h_dsfg7d8f5
