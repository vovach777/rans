/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Vladimir Poslavskiy
 * vovach777@yandex.ru
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once
#if ( !((defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || __cplusplus >= 201703L))
#error  "C++17 compiller required."
#include <stophere>
#endif
#include <iostream>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <string_view>
#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>
namespace myargs {
using namespace std::literals;
#ifndef DUMMY
#define DUMMY
#endif
template<typename T, typename S>
auto str(T && obj,S && sep) -> decltype( obj.begin(), obj.end(),   std::declval<std::ostream&>() << std::declval<S>(),  std::string() )
{
    std::stringstream ss;
    auto begin = obj.begin();
    auto end   = obj.end();
    bool first = true;
    for (; begin != end; begin++)
    {
        if (!first)
            ss << sep;
        ss << *begin;
        first = false;
    }
    return ss.str();
}

template<typename T>
auto str(T && obj) -> decltype( obj.begin(), obj.end(), std::string() )
{
   return str(obj,", ");
}

using ArgsMap = std::unordered_map<std::string,std::vector<std::string>>;
using ArgsMapiList = std::initializer_list<ArgsMap::value_type>;

namespace converter {
   template<typename T, typename Dummy=decltype(  ArgsMap().count(std::declval<T>()))>
   decltype(auto) convert_key(T && v) {
      return std::forward<T>(v);
   }

   static auto convert_key(std::string_view v) {
      return std::string{v};
   }

   static auto convert_key(char v) {
      return std::string{v};
   }

   template<typename T, typename Dummy=decltype( std::string_view(std::declval<T>()) ) >
   std::string_view sv(T && v) {
      return std::string_view(v);
   }
   static std::string_view sv(std::string_view v) {
      return v;
   }
}

template<typename T, typename U=decltype(std::declval<std::istream&>() >> std::declval< std::decay_t<T> &>())>
T parse_as(const std::string & sv, T default_v=T() )
{
   if (sv.size() == 0)
      return default_v;
   T i;
   std::stringstream ss(sv);
   ss >> i;
   return ss.fail() ? default_v : i;
}

template<typename K, typename T, typename = decltype( converter::convert_key( std::declval<K>())),typename = std::enable_if_t<std::is_arithmetic_v<T>>>
void __get_n_number(K&& opt, int index, T default_v, T min, T max );

template<typename K, typename T, typename U=decltype( converter::convert_key( std::declval<K>()), converter::sv(std::declval<T>()))>
void __get_n_str(K&& opt, int index, T default_v);


class Args
{
   private:
   std::unordered_map<std::string,std::string> group_map{};
   ArgsMap m{};
   ArgsMap where{};
   std::vector<std::string> dummy{};
   std::string dummy2{};
   std::string _g(std::string_view arg)
   {
      std::string sarg(arg);
      return group_map.count( sarg ) ? group_map[sarg] : sarg;
   }
   public:
   using iterator = ArgsMap::iterator;
   iterator begin() { return m.begin(); }
   iterator end() { return m.end(); }
   Args() = default;

   template<typename T>
   void group(  T begin, T end )
   {
      for (;begin != end; ++begin) {
         auto &kv = *begin;
         if (kv.first.size())
         {
            for (auto& s : kv.second)
            {
               if (s.size())
                  group_map[ s ] = kv.first;
            }
         }
      }
   }


   void group(ArgsMap map )
   {
      group(map.begin(), map.end());
   }

   void group(ArgsMapiList ilist )
   {
      group(ilist.begin(), ilist.end());
   }


   template<typename T, typename T2, typename U=decltype( converter::convert_key( std::declval<T>()), converter::convert_key( std::declval<T2>()) )>
   void add_to_group( T&& group, T2&& opt) {
      group_map[ converter::convert_key(opt) ] = converter::convert_key(group);
   }

   void parse(int argc, char**argv)
   {
      m.clear();
      m.reserve(argc);
      where.clear();
      where.reserve(argc);
      for (char **it = argv, **end = argv+argc; it < end; it++)
      {
         auto arg = std::string_view( *it );
         if (arg.empty() )
            continue;
         auto prefix_len=0;
         while (arg[0] == '-') {
            arg.remove_prefix(1);
            prefix_len++;
         }

         if (prefix_len==0) {
            m[""].emplace_back(arg);
            where[""].emplace_back(arg);
         } else
         {
            std::string key;
            if (prefix_len==1) {
               m[ key=_g( arg.substr(0,1) ) ].emplace_back( arg.substr(1) );
               //where[ _g( arg.substr(0,1) ) ].emplace_back( arg );
            }
            else {
               auto pos = arg.find('=');
               if (pos == std::string::npos) {
                  m[ key=_g(arg) ].emplace_back();
                  //where[ _g(arg) ].emplace_back( arg );
               }
               else {
                  m[ key=_g(arg.substr(0,pos)) ].emplace_back( arg.substr(pos+1) );
                  //where[ _g(arg.substr(0,pos)) ].emplace_back( arg );
               }
            }
            where[key].emplace_back(arg);
         }
      }
   }


   template<typename T, typename U=decltype( converter::convert_key( std::declval<T>()))>
   const std::string& operator [](T&&opt)
   {
      return has(opt) ? m[converter::convert_key(opt)].at(0) : dummy2;
   }

   template<typename T, typename U=decltype( converter::convert_key( std::declval<T>()))>
   const std::vector<std::string>& all(T&&key)
   {
      return  m.count( converter::convert_key(key) ) == 0 ? dummy :  m[converter::convert_key(key) ];
   }



   template<typename T, typename = decltype( converter::convert_key( std::declval<T>()))>
   auto count(T&&opt)
   {
      return all(opt).size();
   }

   template<typename T, typename = decltype( converter::convert_key( std::declval<T>()))>
   bool has(T && opt)
   {
      return count(opt) > 0;
   }


   template<typename T, typename = decltype( converter::convert_key( std::declval<T>()))>
   bool has(T && opt, int index)
   {
      auto size = count(opt);
      if (index < 0)
          index = size + index;
      return index >= 0 && index < size;
   }

   const std::string& operator [](int arg)
   {
      return all("").at(arg);
   }
   auto size() {
      return all("").size();
   }


   template<typename T, typename = decltype( converter::convert_key( std::declval<T>()))>
   const std::string& str_n(T && opt, int index)
   {
      auto size = count(opt);
      if (index < 0)
         index = size + index;
      if (index >= 0 && index < size)
         return all(opt)[index];
      else
         return dummy2;
   }

   template<typename T, typename = decltype( converter::convert_key( std::declval<T>()))>
   const std::string& last(T&&key)
   {
      return str_n(key,-1);
   }


   template<typename T, typename K, typename = decltype( converter::convert_key( std::declval<K>())),typename =std::enable_if_t<std::is_arithmetic_v<T>> >
   T get_n_number(K&& opt, int index, T default_v, T _min, T _max )
   {
      return std::clamp<T>( parse_as<T>( str_n(std::forward<K>(opt),index), default_v), _min, _max);
   }

   template<typename T, typename K, typename = decltype( converter::convert_key( std::declval<K>()), converter::sv(std::declval<T>()))  >
   std::string_view get_n_str(K&& opt, int index, T default_v)
   {
      auto & v  =  str_n( std::forward<K>(opt),index);
      return converter::sv( &v == &dummy2 ? default_v : v );
   }


   template<typename T,typename K, typename = decltype( __get_n_number(std::declval<K>(),0,T(),T(),T()) )>
   T get(K&& opt, T default_v=T{}, T _min=T(std::numeric_limits<T>::lowest()),
                                   T _max=T(std::numeric_limits<T>::max DUMMY () ))
   {
      return get_n_number<T>(std::forward<K>(opt),0,default_v,_min,_max);
   }


   template<typename T,typename K, typename = decltype( __get_n_number(std::declval<K>(),0,T(),T(),T()) )>
   T get_last(K&& opt, T default_v=T{}, T _min=std::numeric_limits<T>::lowest(),
                                       T _max=std::numeric_limits<T>::max DUMMY () )
   {
      return get_n_number<T>(std::forward<K>(opt),-1,default_v,_min,_max);
   }

      template<typename T, typename K, typename = decltype( __get_n_number(std::declval<K>(),0,T(),T(),T()) )>
   T get_n(K&& opt, int index, T default_v=T{}, T _min=std::numeric_limits<T>::lowest(),
                                   T _max=std::numeric_limits<T>::max DUMMY () )
   {
      return get_n_number<T>(std::forward<K>(opt),index,default_v,_min,_max);
   }

   template <typename K, typename = decltype( converter::convert_key( std::declval<K>()))>
   int64_t get(K&& opt) {
      return get<int64_t>( std::forward<K>(opt) );
   }

   template<typename T, typename K, typename = decltype( __get_n_str(std::declval<K>(),0, std::declval<T>() ))  >
   std::string_view get(K&& opt, T && default_v)
   {

      return get_n_str(std::forward<K>(opt),0,std::forward<T>(default_v));
   }


   template<typename T, typename K, typename = decltype( __get_n_str(std::declval<K>(),0, std::declval<T>() ))  >
   std::string_view get_n(K&& opt, int index, T && default_v)
   {
      return get_n_str(std::forward<K>(opt),index,std::forward<T>(default_v));
   }

   template<typename T, typename K, typename = decltype( __get_n_str(std::declval<K>(),0, std::declval<T>() ))  >
   std::string_view get_last(K&& opt, T && default_v)
   {
      return get_n_str(std::forward<K>(opt),-1,std::forward<T>(default_v));
   }


#if 0
   template<typename T, typename U=decltype( converter::convert_key( std::declval<T>()))>
   void group_after_parse(T&&primary_name, std::initializer_list<std::string> il_aliases)
   {

      auto&& key = converter::convert_key(primary_name);
      auto& values = m[ key ];

      for (auto& v : il_aliases)
      {
         if (v != key && count(v) > 0 )
         {
            auto&vec = all(v);
            std::move(vec.begin(), vec.end(),  std::back_inserter(values));
            m.erase(v);
         }
      }
      if (values.size() == 0)
         m.erase( key );
   }

   template<typename T, typename V, typename U= decltype( std::declval<std::ostream&>() << std::declval<V>(), converter::convert_key(std::declval<T>()))>
   const std::vector<std::string>& if_set(T&&key, V&&value)
   {
      std::stringstream ss;
      ss << value;
      auto& v = m[ converter::convert_key(key)];
      if (v.size() == 0 ){
         v.push_back(ss.str());
      }
      return v;
   }
   #endif

   template<typename T, typename = decltype(converter::convert_key(std::declval<T>()))>
   decltype(auto) real_opt( T&&opt, int index )
   {
      auto& vec = where[ converter::convert_key(std::forward<T>(opt))];
      return std::as_const( vec[( index<0) ? vec.size()+index : index ] );

   }

   template<typename T, typename = decltype(converter::convert_key(std::declval<T>()))>
   auto real_opt( T&&opt, std::string& find_s )
   {

      const auto& key = converter::convert_key(std::forward<T>(opt));
      auto it = where[ key ].begin();
      auto end = where[key].end();

      //std::cout << " {" << str( where[ std::string(opt) ] ) << "} ";
      if (it == where[key].end())
         return find_s;

      for (auto &s : m[ key ] )
      {
            if (it == end)
               break;
            if (&s == &find_s)
               return *it;
            ++it;
      }
      return std::string(opt) + "(" + find_s + ")";

   }

};

inline Args args;

}
